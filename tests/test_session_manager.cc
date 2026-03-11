#include "http/session/session_manager.h"
#include "sylar/fiber/iomanager.h"
#include "log/logger.h"

#include <assert.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// 测试日志器：复用 system logger，保持测试输出风格一致。
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 用例1：验证 create/get 的基础闭环。
void test_create_and_get() {
    // 创建 SessionManager，最大非活跃时间设为 1000ms。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));
    // 创建一个新会话。
    sylar::http::Session::ptr session = manager->create();
    // 断言会话创建成功。
    assert(session);
    // 设置会话属性 user=alice。
    session->set("user", "alice");

    // 按 SID 取回会话。
    sylar::http::Session::ptr loaded = manager->get(session->getId());
    // 断言能取回。
    assert(loaded);
    // 断言 SID 一致。
    assert(loaded->getId() == session->getId());
    // 断言属性值一致。
    assert(loaded->get("user") == "alice");
}

// 用例2：验证 getOrCreate 的“命中复用/未命中新建”语义。
void test_get_or_create_reuse() {
    // 创建 SessionManager。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));

    // 第一次请求：没有 SID，预期新建会话。
    sylar::http::HttpRequest::ptr request1(new sylar::http::HttpRequest());
    // 第一次请求对应响应对象。
    sylar::http::HttpResponse::ptr response1(new sylar::http::HttpResponse());
    // 调用 getOrCreate，预期创建并下发 SID cookie。
    sylar::http::Session::ptr session1 = manager->getOrCreate(request1, response1);
    // 断言 session 创建成功。
    assert(session1);
    // 断言响应里包含 Set-Cookie。
    assert(!response1->getSetCookies().empty());

    // 第二次请求：携带第一次返回的 SID，预期复用旧会话。
    sylar::http::HttpRequest::ptr request2(new sylar::http::HttpRequest());
    // 第二次请求对应响应对象。
    sylar::http::HttpResponse::ptr response2(new sylar::http::HttpResponse());
    // 在请求 cookie 中写入 SID。
    request2->setCookie("SID", session1->getId());
    // 再次调用 getOrCreate，预期命中旧会话。
    sylar::http::Session::ptr session2 = manager->getOrCreate(request2, response2);
    // 断言会话存在。
    assert(session2);
    // 断言两次返回 SID 相同（复用成功）。
    assert(session2->getId() == session1->getId());
    // 复用旧会话时不应再下发新的 Set-Cookie。
    assert(response2->getSetCookies().empty());
}

// 用例3：验证会话过期与 sweepExpired 行为。
void test_expire_and_sweep() {
    // 创建 SessionManager，过期时间设置得较短（20ms）以便测试。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(20));
    // 连续创建两个会话。
    sylar::http::Session::ptr session1 = manager->create();
    // 创建第二个会话。
    sylar::http::Session::ptr session2 = manager->create();
    // 断言创建成功。
    assert(session1 && session2);

    // 睡眠 40ms，确保会话超过 20ms 过期阈值。
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    // 先通过 get 触发一次按需过期清理：session1 应返回空。
    assert(!manager->get(session1->getId()));
    // 再执行批量 sweep，预期还能扫掉一个（session2）。
    size_t swept = manager->sweepExpired();
    // 断言本轮 sweep 数量为 1。
    assert(swept == 1);
    // 断言 session2 也已不可用。
    assert(!manager->get(session2->getId()));
}

// 用例4：验证定时器驱动的自动 sweep。
void test_timer_sweep() {
    // 创建单线程 IOManager，作为 TimerManager 使用。
    sylar::IOManager iom(1, false, "session_timer_test");
    // 创建 SessionManager，过期时间 30ms。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(30));
    // 创建一个会话。
    sylar::http::Session::ptr session = manager->create();
    // 断言创建成功。
    assert(session);
    // 启动周期清理定时器（每 10ms 一次）。
    assert(manager->startSweepTimer(&iom, 10));
    // 断言定时器状态为已启动。
    assert(manager->hasSweepTimer());

    // 等待足够时间，让会话过期并被定时清理。
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    // 断言会话已被清理。
    assert(!manager->get(session->getId()));
    // 停止定时器。
    assert(manager->stopSweepTimer());
    // 断言定时器状态为未启动。
    assert(!manager->hasSweepTimer());
}

// 用例5：验证并发 create/get 的线程安全基础行为。
void test_concurrent_access() {
    // 创建 SessionManager。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));
    // 原子计数器：统计创建总数。
    std::atomic<int> created(0);
    // 线程容器。
    std::vector<std::thread> threads;

    // 启动 4 个线程并发创建/读取会话。
    for (int i = 0; i < 4; ++i) {
        // 把线程对象放入容器。
        threads.push_back(std::thread([manager, &created]() {
            // 每个线程循环 100 次。
            for (int j = 0; j < 100; ++j) {
                // 创建会话。
                sylar::http::Session::ptr session = manager->create();
                // 断言创建成功。
                assert(session);
                // 原子递增创建计数。
                ++created;
                // 断言刚创建的 SID 可以被 get 命中。
                assert(manager->get(session->getId()));
            }
        }));
    }

    // 等待所有线程执行完成。
    for (size_t i = 0; i < threads.size(); ++i) {
        // join 第 i 个线程。
        threads[i].join();
    }

    // 断言总创建量为 4 * 100 = 400。
    assert(created == 400);
}

// 用例6：验证第四阶段新增的 SessionStorage 注入能力。
void test_session_storage_injection() {
    // 构造默认内存存储实现。
    sylar::http::SessionStorage::ptr storage(new sylar::http::MemorySessionStorage());
    // 将 storage 注入 SessionManager。
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000, storage));
    // 通过 manager 创建会话。
    sylar::http::Session::ptr session = manager->create();
    // 断言创建成功。
    assert(session);
    // 设置一个业务属性 role=admin。
    session->set("role", "admin");
    // 显式保存到 storage（这里用于直接验证 storage 接口语义）。
    storage->save(session);

    // 直接从 storage 加载会话。
    sylar::http::Session::ptr loaded = storage->load(session->getId());
    // 断言可加载。
    assert(loaded);
    // 断言属性值正确。
    assert(loaded->get("role") == "admin");
    // 删除该会话并断言删除成功。
    assert(storage->remove(session->getId()));
    // 再次加载应为空。
    assert(!storage->load(session->getId()));
}

// 测试主函数：顺序执行所有 SessionManager 相关用例。
int main() {
    // 运行 create/get 基础测试。
    test_create_and_get();
    // 运行 getOrCreate 复用测试。
    test_get_or_create_reuse();
    // 运行过期与 sweep 测试。
    test_expire_and_sweep();
    // 运行定时器 sweep 测试。
    test_timer_sweep();
    // 运行并发访问测试。
    test_concurrent_access();
    // 运行 SessionStorage 注入测试。
    test_session_storage_injection();
    // 打印测试通过日志。
    SYLAR_LOG_INFO(g_logger) << "test_session_manager passed";
    // 返回 0 表示程序正常退出。
    return 0;
}

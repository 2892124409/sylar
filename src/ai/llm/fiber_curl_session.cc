#include "ai/llm/fiber_curl_session.h"

#include "log/logger.h"

#include <vector>

namespace ai
{
namespace llm
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造 fiber-curl 会话并捕获当前执行上下文。
 */
FiberCurlSession::FiberCurlSession(CURL* easy)
    : m_easy(easy)
    , m_multi(nullptr)
    , m_iom(sylar::IOManager::GetThis())
    , m_wait_thread(-1)
    , m_waiting(false)
    , m_resume_scheduled(false)
    , m_done(false)
    , m_result(CURLE_OK)
{
    if (m_iom)
    {
        m_wait_fiber = sylar::Fiber::GetThis();
        if (m_wait_fiber)
        {
            m_wait_thread = m_wait_fiber->getBoundThread();
        }
    }
}

/**
 * @brief 析构时统一清理。
 */
FiberCurlSession::~FiberCurlSession()
{
    Cleanup();
}

/**
 * @brief libcurl socket 回调入口。
 */
int FiberCurlSession::SocketCallback(CURL*, curl_socket_t s, int what, void* userp, void*)
{
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    if (!self)
    {
        return 0;
    }

    self->RegisterSocketWatch(s, what);
    return 0;
}

/**
 * @brief libcurl timer 回调入口。
 */
int FiberCurlSession::TimerCallback(CURLM*, long timeout_ms, void* userp)
{
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    if (!self)
    {
        return 0;
    }

    self->UpdateTimer(timeout_ms);
    return 0;
}

/**
 * @brief 记录 socket action，并在必要时恢复等待中的 fiber。
 */
void FiberCurlSession::OnSocketEvent(curl_socket_t fd, int action)
{
    // sylar IOManager 的 fd 事件是一次性触发语义，触发后会自动从 epoll 中移除。
    // libcurl 的 socket 回调并不会保证“每次触发都重新下发关注事件”。
    // 因此在收到一次 fd 事件后，需要按当前 watch 配置主动重挂载一次监听。
    if (fd != CURL_SOCKET_TIMEOUT)
    {
        // 先把本次触发的事件位从“已挂载状态”中剔除，再按目标事件位补齐。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::map<curl_socket_t, int>::iterator it = m_armed_events.find(fd);
            if (it != m_armed_events.end())
            {
                if (action & CURL_CSELECT_IN)
                {
                    it->second &= ~sylar::IOManager::READ;
                }
                if (action & CURL_CSELECT_OUT)
                {
                    it->second &= ~sylar::IOManager::WRITE;
                }
            }
        }
        RearmSocketWatch(fd);
    }

    bool need_schedule = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending_actions.push_back(std::make_pair(fd, action));
        if (m_waiting && !m_resume_scheduled)
        {
            m_resume_scheduled = true;
            need_schedule = true;
        }
    }

    if (need_schedule && m_iom && m_wait_fiber)
    {
        m_iom->schedule(m_wait_fiber, m_wait_thread);
    }
}

/**
 * @brief 根据 libcurl 关注事件注册 fd 监听。
 */
void FiberCurlSession::RegisterSocketWatch(curl_socket_t fd, int what)
{
    if (!m_iom)
    {
        return;
    }

    if (what == CURL_POLL_REMOVE)
    {
        CancelSocketWatch(fd);
        return;
    }

    const int target_events = ((what == CURL_POLL_IN || what == CURL_POLL_INOUT) ? sylar::IOManager::READ : 0) |
                              ((what == CURL_POLL_OUT || what == CURL_POLL_INOUT) ? sylar::IOManager::WRITE : 0);

    int to_del = 0;
    int to_add = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int armed = m_armed_events[fd];
        m_watch_events[fd] = target_events;
        to_del = armed & ~target_events;
        to_add = target_events & ~armed;
        // 乐观更新：避免并发回调重复 addEvent 触发断言。
        m_armed_events[fd] = (armed & ~to_del) | to_add;
    }

    if (to_del & sylar::IOManager::READ)
    {
        m_iom->delEvent(fd, sylar::IOManager::READ);
    }

    if (to_del & sylar::IOManager::WRITE)
    {
        m_iom->delEvent(fd, sylar::IOManager::WRITE);
    }

    // 注册新增监听位。
    if (to_add & sylar::IOManager::READ)
    {
        int rt = m_iom->addEvent(fd, sylar::IOManager::READ,
                                 [this, fd]() { OnSocketEvent(fd, CURL_CSELECT_IN); });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "add READ event failed for fd=" << fd;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_armed_events[fd] &= ~sylar::IOManager::READ;
        }
    }

    if (to_add & sylar::IOManager::WRITE)
    {
        int rt = m_iom->addEvent(fd, sylar::IOManager::WRITE,
                                 [this, fd]() { OnSocketEvent(fd, CURL_CSELECT_OUT); });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "add WRITE event failed for fd=" << fd;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_armed_events[fd] &= ~sylar::IOManager::WRITE;
        }
    }
}

/**
 * @brief 在 sylar 一次性事件模型下重挂载 fd 监听。
 */
void FiberCurlSession::RearmSocketWatch(curl_socket_t fd)
{
    if (!m_iom)
    {
        return;
    }

    int to_add = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::map<curl_socket_t, int>::const_iterator desired_it = m_watch_events.find(fd);
        if (desired_it == m_watch_events.end())
        {
            return;
        }
        const int desired = desired_it->second;
        const int armed = m_armed_events[fd];
        to_add = desired & ~armed;
        // 乐观更新：并发回调下让后续线程看到“已计划挂载”的状态，避免重复 addEvent。
        m_armed_events[fd] = armed | to_add;
    }

    if (to_add & sylar::IOManager::READ)
    {
        int rt = m_iom->addEvent(fd, sylar::IOManager::READ,
                                 [this, fd]() { OnSocketEvent(fd, CURL_CSELECT_IN); });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "rearm READ event failed for fd=" << fd;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_armed_events[fd] &= ~sylar::IOManager::READ;
        }
    }

    if (to_add & sylar::IOManager::WRITE)
    {
        int rt = m_iom->addEvent(fd, sylar::IOManager::WRITE,
                                 [this, fd]() { OnSocketEvent(fd, CURL_CSELECT_OUT); });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "rearm WRITE event failed for fd=" << fd;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_armed_events[fd] &= ~sylar::IOManager::WRITE;
        }
    }
}

/**
 * @brief 删除指定 fd 的读写监听。
 */
void FiberCurlSession::CancelSocketWatch(curl_socket_t fd)
{
    if (!m_iom)
    {
        return;
    }

    int armed = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::map<curl_socket_t, int>::const_iterator it = m_armed_events.find(fd);
        if (it != m_armed_events.end())
        {
            armed = it->second;
        }
        m_watch_events.erase(fd);
        m_armed_events.erase(fd);
    }

    if (armed & sylar::IOManager::READ)
    {
        m_iom->delEvent(fd, sylar::IOManager::READ);
    }

    if (armed & sylar::IOManager::WRITE)
    {
        m_iom->delEvent(fd, sylar::IOManager::WRITE);
    }
}

/**
 * @brief 更新 libcurl 需要的超时定时器。
 */
void FiberCurlSession::UpdateTimer(long timeout_ms)
{
    if (!m_iom)
    {
        return;
    }

    if (m_timer)
    {
        m_timer->cancel();
        m_timer.reset();
    }

    if (timeout_ms < 0)
    {
        return;
    }

    if (timeout_ms == 0)
    {
        OnSocketEvent(CURL_SOCKET_TIMEOUT, 0);
        return;
    }

    m_timer =
        m_iom->addTimer(static_cast<uint64_t>(timeout_ms), [this]() { OnSocketEvent(CURL_SOCKET_TIMEOUT, 0); }, false);
}

/**
 * @brief 若无待处理事件则挂起当前 fiber，等待 IO/Timer 唤醒。
 * @details
 * 执行时序：
 * 1) 先在锁内检查 `m_pending_actions`，若队列非空则直接返回，不发生挂起；
 * 2) 若队列为空，标记 `m_waiting=true`，并把 `m_resume_scheduled` 复位；
 * 3) 调用 `sylar::Fiber::YieldToHold()` 让出当前协程执行权；
 * 4) 当 socket/timer 回调通过 `schedule(m_wait_fiber, ...)` 恢复后，从此函数返回；
 * 5) 返回前清理等待态标记，避免下一轮误判为“仍在等待”。
 */
void FiberCurlSession::WaitForSignal()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pending_actions.empty())
        {
            return;
        }
        m_waiting = true;
        m_resume_scheduled = false;
    }

    sylar::Fiber::YieldToHold();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_waiting = false;
    m_resume_scheduled = false;
}

/**
 * @brief 从待处理队列取出一个 socket action。
 */
bool FiberCurlSession::PopPendingAction(curl_socket_t& fd, int& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending_actions.empty())
    {
        return false;
    }

    std::pair<curl_socket_t, int> item = m_pending_actions.front();
    m_pending_actions.pop_front();
    fd = item.first;
    action = item.second;
    return true;
}

/**
 * @brief 消费 multi 消息队列，提取请求完成状态。
 * @details
 * 执行时序：
 * 1) 若 `m_multi` 为空，说明会话未进入 multi 模式，直接返回；
 * 2) 循环调用 `curl_multi_info_read(m_multi, &remaining)` 拉取完成消息；
 * 3) 遇到 `CURLMSG_DONE` 时，写入 `m_done=true` 和 `m_result`；
 * 4) 当无更多消息时退出循环，控制权回到调用方继续驱动状态机。
 */
void FiberCurlSession::DrainMessages()
{
    if (!m_multi)
    {
        return;
    }

    int remaining = 0;
    while (true)
    {
        CURLMsg* msg = curl_multi_info_read(m_multi, &remaining);
        if (!msg)
        {
            break;
        }

        if (msg->msg == CURLMSG_DONE)
        {
            m_done = true;
            m_result = msg->data.result;
        }
    }
}

/**
 * @brief 统一清理资源（timer、fd 监听、multi handle）。
 * @details
 * 清理时序：
 * 1) 先取消并释放定时器，防止后续回调再写入已销毁对象；
 * 2) 收集并清空当前已注册监听的 fd 集合，同时清空待处理 action 队列；
 * 3) 逐个删除 IOManager 上的 READ/WRITE 监听；
 * 4) 若存在 multi handle，则移除 easy handle 并 `curl_multi_cleanup(...)`。
 *
 * @note 该函数可能在错误路径、正常结束路径和析构路径重复调用，设计为幂等清理。
 */
void FiberCurlSession::Cleanup()
{
    if (m_timer)
    {
        m_timer->cancel();
        m_timer.reset();
    }

    if (m_iom)
    {
        std::vector<curl_socket_t> fds;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (std::map<curl_socket_t, int>::const_iterator it = m_watch_events.begin(); it != m_watch_events.end();
                 ++it)
            {
                fds.push_back(it->first);
            }
            m_watch_events.clear();
            m_armed_events.clear();
            m_pending_actions.clear();
        }

        for (size_t i = 0; i < fds.size(); ++i)
        {
            m_iom->delEvent(fds[i], sylar::IOManager::READ);
            m_iom->delEvent(fds[i], sylar::IOManager::WRITE);
        }
    }

    if (m_multi)
    {
        if (m_easy)
        {
            curl_multi_remove_handle(m_multi, m_easy);
        }
        curl_multi_cleanup(m_multi);
        m_multi = nullptr;
    }
}

/**
 * @brief 执行一次请求。
 * @details
 * 该函数是 FiberCurlSession 的核心状态机，分为两条路径：
 * 1) 协程路径：有 IOManager 且有当前 Fiber，走 `curl_multi + socket/timer + YieldToHold`；
 * 2) 回退路径：无协程上下文时，直接阻塞调用 `curl_easy_perform`。
 *
 * 协程路径的关键目标是：当前 Fiber 在网络等待期间挂起，让出工作线程给其他 Fiber，
 * 当 fd 可读/可写或超时事件到来时再恢复继续执行。
 */
CURLcode FiberCurlSession::Perform()
{
    // ---- Step 0: 基础入参校验 ----
    // m_easy 为空说明上层没有正确初始化 easy handle，无法发起请求。
    if (!m_easy)
    {
        return CURLE_FAILED_INIT;
    }

    // ---- Step 1: 判断是否具备“协程化驱动”条件 ----
    // 条件不满足（没有 IOM 或当前线程不在 Fiber 上）时，直接走阻塞回退路径。
    // 这个分支保证了在测试线程/普通线程中也能工作，不依赖协程调度器。
    if (!m_iom || !m_wait_fiber)
    {
        // 回退为传统阻塞请求：线程会被占住直到请求完成或超时。
        return curl_easy_perform(m_easy);
    }

    // ---- Step 2: 初始化 multi handle ----
    // 后续所有“事件驱动推进”都通过 m_multi 完成。
    m_multi = curl_multi_init();
    if (!m_multi)
    {
        return CURLE_FAILED_INIT;
    }

    // ---- Step 3: 注册 libcurl 回调入口 ----
    // SOCKETFUNCTION: 告诉我们应该监听哪些 fd 的读写事件。
    // TIMERFUNCTION:  告诉我们下一次超时应在多久后触发。
    // *_DATA 统一传 this，回调里可还原到当前会话对象。
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETFUNCTION, &FiberCurlSession::SocketCallback);
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERFUNCTION, &FiberCurlSession::TimerCallback);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERDATA, this);

    // ---- Step 4: 把 easy handle 挂载到 multi 状态机 ----
    // 从此以后，easy 的网络生命周期由 multi 驱动。
    CURLMcode mcode = curl_multi_add_handle(m_multi, m_easy);
    if (mcode != CURLM_OK)
    {
        // 出错时统一走 Cleanup，避免 timer/fd 监听泄漏。
        Cleanup();
        return CURLE_FAILED_INIT;
    }

    // ---- Step 5: 先触发一次“超时事件”启动状态机 ----
    // 这是 libcurl multi 模式常见做法：先踢一脚，让内部决定要监听哪些 fd/定时器。
    int running = 0;
    mcode = curl_multi_socket_action(m_multi, CURL_SOCKET_TIMEOUT, 0, &running);
    if (mcode != CURLM_OK)
    {
        Cleanup();
        return CURLE_RECV_ERROR;
    }

    // 启动后先拉一轮消息，防止“立即完成”场景漏读 DONE。
    DrainMessages();

    // ---- Step 6: 主循环：直到请求完成或无 running 任务 ----
    // m_done=true 由 DrainMessages 读到 CURLMSG_DONE 设置。
    // running 由 curl_multi_socket_action 输出，表示仍有传输在进行。
    while (!m_done && running > 0)
    {
        // 6.1 若当前没有可处理 action，则挂起当前 Fiber。
        // 等待 socket/timer 回调把事件塞进队列并 schedule 回来。
        WaitForSignal();

        // 6.2 批量消费这次唤醒带来的所有 pending action。
        // 每个 action 都会推进一次 multi 状态机。
        curl_socket_t fd = 0;
        int action = 0;
        while (PopPendingAction(fd, action))
        {
            // 用“哪个 fd + 发生了什么事件”推进 libcurl 传输进度。
            mcode = curl_multi_socket_action(m_multi, fd, action, &running);
            if (mcode != CURLM_OK)
            {
                Cleanup();
                return CURLE_RECV_ERROR;
            }
            // 每推进一次都尝试读取 DONE 消息，尽快感知完成态。
            DrainMessages();
        }

        // 6.3 防御性再读一轮消息：
        // 某些情况下 action 消费后 done 消息可能才可见，这里兜底拉取。
        if (!m_done && running > 0)
        {
            DrainMessages();
        }
    }

    // ---- Step 7: 退出循环后的最终兜底 ----
    // while 条件退出不代表一定已经读到 DONE，最后再尝试一次。
    if (!m_done)
    {
        DrainMessages();
    }

    // ---- Step 8: 保存结果并统一清理 ----
    // m_result 默认 CURLE_OK；若收到 DONE 会被实际结果覆盖。
    CURLcode result = m_result;
    Cleanup();
    return result;
}

} // namespace llm
} // namespace ai

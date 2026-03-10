#include "sylar/http/session_manager.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/log/logger.h"

#include <assert.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_create_and_get() {
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));
    sylar::http::Session::ptr session = manager->create();
    assert(session);
    session->set("user", "alice");

    sylar::http::Session::ptr loaded = manager->get(session->getId());
    assert(loaded);
    assert(loaded->getId() == session->getId());
    assert(loaded->get("user") == "alice");
}

void test_get_or_create_reuse() {
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));

    sylar::http::HttpRequest::ptr request1(new sylar::http::HttpRequest());
    sylar::http::HttpResponse::ptr response1(new sylar::http::HttpResponse());
    sylar::http::Session::ptr session1 = manager->getOrCreate(request1, response1);
    assert(session1);
    assert(!response1->getSetCookies().empty());

    sylar::http::HttpRequest::ptr request2(new sylar::http::HttpRequest());
    sylar::http::HttpResponse::ptr response2(new sylar::http::HttpResponse());
    request2->setCookie("SID", session1->getId());
    sylar::http::Session::ptr session2 = manager->getOrCreate(request2, response2);
    assert(session2);
    assert(session2->getId() == session1->getId());
    assert(response2->getSetCookies().empty());
}

void test_expire_and_sweep() {
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(20));
    sylar::http::Session::ptr session1 = manager->create();
    sylar::http::Session::ptr session2 = manager->create();
    assert(session1 && session2);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    assert(!manager->get(session1->getId()));
    size_t swept = manager->sweepExpired();
    assert(swept == 1);
    assert(!manager->get(session2->getId()));
}

void test_timer_sweep() {
    sylar::IOManager iom(1, false, "session_timer_test");
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(30));
    sylar::http::Session::ptr session = manager->create();
    assert(session);
    assert(manager->startSweepTimer(&iom, 10));
    assert(manager->hasSweepTimer());

    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    assert(!manager->get(session->getId()));
    assert(manager->stopSweepTimer());
    assert(!manager->hasSweepTimer());
}

void test_concurrent_access() {
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000));
    std::atomic<int> created(0);
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.push_back(std::thread([manager, &created]() {
            for (int j = 0; j < 100; ++j) {
                sylar::http::Session::ptr session = manager->create();
                assert(session);
                ++created;
                assert(manager->get(session->getId()));
            }
        }));
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    assert(created == 400);
}

void test_session_storage_injection() {
    sylar::http::SessionStorage::ptr storage(new sylar::http::MemorySessionStorage());
    sylar::http::SessionManager::ptr manager(new sylar::http::SessionManager(1000, storage));
    sylar::http::Session::ptr session = manager->create();
    assert(session);
    session->set("role", "admin");
    storage->save(session);

    sylar::http::Session::ptr loaded = storage->load(session->getId());
    assert(loaded);
    assert(loaded->get("role") == "admin");
    assert(storage->remove(session->getId()));
    assert(!storage->load(session->getId()));
}

int main() {
    test_create_and_get();
    test_get_or_create_reuse();
    test_expire_and_sweep();
    test_timer_sweep();
    test_concurrent_access();
    test_session_storage_injection();
    SYLAR_LOG_INFO(g_logger) << "test_session_manager passed";
    return 0;
}

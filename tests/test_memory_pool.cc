#include "http/core/http_memory_pool.h"
#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/server/http_session.h"
#include "http/session/session.h"
#include "memorypool/memory_pool.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static void test_hash_bucket_append_and_fallback_free()
{
    sylar::HashBucket::initMemoryPool(64);

    void *fallback = sylar::HashBucket::useMemory(100);
    assert(fallback != nullptr);

    sylar::HashBucket::initMemoryPool(128);
    sylar::HashBucket::freeMemory(fallback, 100);

    base::MemoryPool &pool = sylar::HashBucket::getMemoryPool(1);
    void *pooled = pool.allocate();
    assert(pooled != nullptr);
    assert(pooled != fallback);
    pool.deallocate(pooled);

    void *routed1 = sylar::HashBucket::useMemory(100);
    assert(routed1 != nullptr);
    sylar::HashBucket::freeMemory(routed1, 100);

    void *routed2 = sylar::HashBucket::useMemory(100);
    assert(routed2 == routed1);
    sylar::HashBucket::freeMemory(routed2, 100);
}

static void test_http_pooled_shared_creation()
{
    http::HttpRequest::ptr request = http::MakeHttpPooledShared<http::HttpRequest>();
    assert(request);
    request->setPath("/memory-pool");
    assert(request->getPath() == "/memory-pool");

    http::HttpResponse::ptr response = http::MakeHttpPooledShared<http::HttpResponse>();
    assert(response);
    response->setBody("ok");
    assert(response->getBody() == "ok");

    http::Session::ptr session = http::MakeHttpPooledShared<http::Session>("sid-1", 1, 1000);
    assert(session);
    session->set("user", "sylar");
    assert(session->get("user") == "sylar");

    http::HttpSession::ptr http_session = http::MakeHttpPooledShared<http::HttpSession>(sylar::Socket::ptr());
    assert(http_session);
}

static void test_http_session_reuse_via_shared_ptr_deleter()
{
    void *first_address = nullptr;
    {
        http::HttpSession::ptr first = http::MakeHttpPooledShared<http::HttpSession>(sylar::Socket::ptr());
        assert(first);
        first_address = first.get();
    }

    http::HttpSession::ptr second = http::MakeHttpPooledShared<http::HttpSession>(sylar::Socket::ptr());
    assert(second);
    if (second.get() != first_address)
    {
        std::cerr << "expected pooled HttpSession reuse" << std::endl;
        std::abort();
    }
}

static void test_memory_pool_alignment()
{
    base::MemoryPool pool;
    pool.init(33);

    const std::size_t alignment = alignof(std::max_align_t);
    void *slot1 = pool.allocate();
    void *slot2 = pool.allocate();

    assert(slot1 != nullptr);
    assert(slot2 != nullptr);
    if (reinterpret_cast<std::uintptr_t>(slot1) % alignment != 0 ||
        reinterpret_cast<std::uintptr_t>(slot2) % alignment != 0)
    {
        std::cerr << "memory pool returned misaligned slot" << std::endl;
        std::abort();
    }

    pool.deallocate(slot1);
    pool.deallocate(slot2);
}

int main()
{
    test_hash_bucket_append_and_fallback_free();
    test_http_pooled_shared_creation();
    test_http_session_reuse_via_shared_ptr_deleter();
    test_memory_pool_alignment();

    std::cout << "test_memory_pool passed" << std::endl;
    return 0;
}

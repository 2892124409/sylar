#include "http/core/http_memory_pool.h"
#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/server/http_session.h"
#include "http/session/session.h"
#include "memorypool/memory_pool.h"

#include <cassert>
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

int main()
{
    test_hash_bucket_append_and_fallback_free();
    test_http_pooled_shared_creation();

    std::cout << "test_memory_pool passed" << std::endl;
    return 0;
}

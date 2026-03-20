/**
 * MemoryPool 功能测试（thread_local 无锁模型）
 *
 * 说明：
 *   当前 MemoryPool 语义为“单实例单线程使用”，推荐通过 getThreadLocalPool<T>()
 *   获取每线程独立池。本测试按该语义验证功能，不覆盖跨线程共享同一池实例的场景。
 */

#include "sylar/memorypool/memory_pool.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <pthread.h>
#include <set>
#include <vector>

namespace
{
    void check(bool cond, const char *msg)
    {
        if (!cond)
        {
            std::cerr << "[CHECK FAILED] " << msg << std::endl;
            std::abort();
        }
    }
}

struct Node {
    int val;
    char pad[60]; // 64B
};

struct CounterNode {
    static int s_ctor;
    static int s_dtor;

    int v;
    explicit CounterNode(int x = 0) : v(x) { ++s_ctor; }
    ~CounterNode() { ++s_dtor; }
};

int CounterNode::s_ctor = 0;
int CounterNode::s_dtor = 0;

static void test_basic_single_thread() {
    std::cout << "[test_basic_single_thread] start\n";

    MemoryPool<Node> pool;

    // 基本分配与回收
    Node* p1 = pool.allocate();
    Node* p2 = pool.allocate();
    check(p1 != nullptr && p2 != nullptr, "allocate returned nullptr");
    check(p1 != p2, "two consecutive allocations returned same pointer");
    p1->val = 11;
    p2->val = 22;
    check(p1->val == 11 && p2->val == 22, "written values mismatch");

    pool.deallocate(p1);
    pool.deallocate(p2);

    // LIFO 复用检查：最后释放的 p2 应该先被取回
    Node* p3 = pool.allocate();
    check(p3 == p2, "LIFO reuse mismatch");
    pool.deallocate(p3);

    // 扩容 + 大量对象读写
    const int n = 50000;
    std::vector<Node*> ptrs;
    ptrs.reserve(n);
    for (int i = 0; i < n; ++i) {
        Node* p = pool.allocate();
        p->val = i;
        ptrs.push_back(p);
    }
    for (int i = 0; i < n; ++i) {
        check(ptrs[i]->val == i, "batch value mismatch");
    }
    for (int i = 0; i < n; ++i) {
        pool.deallocate(ptrs[i]);
    }

    std::cout << "[test_basic_single_thread] PASS\n";
}

static void test_new_delete_element() {
    std::cout << "[test_new_delete_element] start\n";

    CounterNode::s_ctor = 0;
    CounterNode::s_dtor = 0;

    MemoryPool<CounterNode> pool;

    CounterNode* a = pool.newElement(7);
    CounterNode* b = pool.newElement(9);
    check(a != nullptr && b != nullptr, "newElement returned nullptr");
    check(a->v == 7, "constructor arg mismatch for a");
    check(b->v == 9, "constructor arg mismatch for b");

    pool.deleteElement(a);
    pool.deleteElement(b);

    check(CounterNode::s_ctor == 2, "constructor count mismatch");
    check(CounterNode::s_dtor == 2, "destructor count mismatch");

    std::cout << "[test_new_delete_element] PASS\n";
}

struct ThreadArgs {
    int tid;
    int iters;
    uintptr_t tls_pool_addr;
    bool ok;
};

static void* tls_pool_worker(void* arg) {
    ThreadArgs* a = static_cast<ThreadArgs*>(arg);
    a->ok = true;

    // 每线程独立池
    auto& pool = getThreadLocalPool<Node>();
    a->tls_pool_addr = reinterpret_cast<uintptr_t>(&pool);

    // 高频 allocate/deallocate，验证稳定性
    for (int i = 0; i < a->iters; ++i) {
        Node* p = pool.allocate();
        if (!p) {
            a->ok = false;
            return nullptr;
        }
        p->val = (a->tid << 20) ^ i;
        if (p->val != ((a->tid << 20) ^ i)) {
            a->ok = false;
            return nullptr;
        }
        pool.deallocate(p);
    }

    // 批量对象，覆盖块扩容路径
    std::vector<Node*> batch;
    batch.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        Node* p = pool.allocate();
        p->val = i + a->tid;
        batch.push_back(p);
    }
    for (int i = 0; i < 10000; ++i) {
        if (batch[i]->val != i + a->tid) {
            a->ok = false;
            return nullptr;
        }
    }
    for (auto* p : batch) {
        pool.deallocate(p);
    }

    return nullptr;
}

static void test_thread_local_parallel() {
    std::cout << "[test_thread_local_parallel] start\n";

    const int nthreads = 8;
    const int iters = 200000;

    uintptr_t main_tls_addr = reinterpret_cast<uintptr_t>(&getThreadLocalPool<Node>());

    std::vector<ThreadArgs> args(nthreads);
    std::vector<pthread_t> tids(nthreads);

    for (int i = 0; i < nthreads; ++i) {
        args[i].tid = i + 1;
        args[i].iters = iters;
        args[i].tls_pool_addr = 0;
        args[i].ok = false;
        int rc = pthread_create(&tids[i], nullptr, tls_pool_worker, &args[i]);
        check(rc == 0, "pthread_create failed");
    }

    for (int i = 0; i < nthreads; ++i) {
        int rc = pthread_join(tids[i], nullptr);
        check(rc == 0, "pthread_join failed");
    }

    std::set<uintptr_t> uniq;
    for (int i = 0; i < nthreads; ++i) {
        check(args[i].ok, "worker check failed");
        check(args[i].tls_pool_addr != 0, "tls pool address is zero");
        uniq.insert(args[i].tls_pool_addr);
    }

    // 所有线程应各自拥有不同的 TLS pool 实例地址
    check(static_cast<int>(uniq.size()) == nthreads, "TLS pool addresses should be unique per worker thread");
    check(uniq.count(main_tls_addr) == 0, "main thread TLS pool address leaked into worker set");

    std::cout << "[test_thread_local_parallel] PASS\n";
}

int main() {
    test_basic_single_thread();
    test_new_delete_element();
    test_thread_local_parallel();
    std::cout << "\nAll memorypool tests PASSED.\n";
    return 0;
}

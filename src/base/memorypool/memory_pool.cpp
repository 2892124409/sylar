#include "memory_pool.h"

#include <algorithm>
#include <unordered_set>

namespace base
{
    namespace
    {
        static std::vector<int> NormalizeSlotSizes(std::initializer_list<int> slotSizes)
        {
            std::vector<int> normalized(slotSizes.begin(), slotSizes.end());
            normalized.erase(
                std::remove_if(normalized.begin(), normalized.end(),
                               [](int size)
                               { return size <= 0; }),
                normalized.end());
            std::sort(normalized.begin(), normalized.end());
            normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
            return normalized;
        }

        // 记录所有退化到系统分配器的指针，避免追加新桶后 freeMemory() 误回收到池里。
        static std::unordered_set<void *> &GetFallbackAllocations()
        {
            static std::unordered_set<void *> fallbackAllocations;
            return fallbackAllocations;
        }

        static std::mutex &GetFallbackMutex()
        {
            static std::mutex fallbackMutex;
            return fallbackMutex;
        }
    } // namespace


    /**
     * @brief 构造内存池并初始化成员。
     *
     * @param blockSize 每次向系统申请的内存块大小（字节）。
     *
     * @details
     * - BlockSize_：控制一次扩容时申请多大块连续内存。
     * - SlotSize_：在 init() 后才会设置为具体槽位大小。
     * - firstBlock_：维护所有大块内存的链表头，析构时用于统一释放。
     * - curSlot_ / lastSlot_：描述当前大块中“可线性切分”的槽位范围。
     * - freeList_：存放被回收的槽位，优先复用。
     */
    MemoryPool::MemoryPool(int blockSize)
        : BlockSize_(blockSize), SlotSize_(0), firstBlock_(nullptr), curSlot_(nullptr), freeList_(nullptr), lastSlot_(nullptr)
    {
        assert(blockSize > 0 && "blockSize must be positive");
    }

    /**
     * @brief 析构内存池，释放所有申请过的大块内存。
     *
     * @details
     * firstBlock_ 链表中挂的是每次 allocateNewBlock() 申请到的大块内存首地址。
     * 逐个 operator delete 后，内存池管理的原始内存会全部归还给系统。
     */
    MemoryPool::~MemoryPool()
    {
        Node *cur = firstBlock_;
        while (cur)
        {
            Node *next = cur->next;
            ::operator delete(static_cast<void *>(cur));
            cur = next;
        }
    }

    /**
     * @brief 初始化当前内存池的槽位大小并重置状态。
     *
     * @param slotSize 槽位大小（字节），必须大于 0。
     *
     * @warning
     * 该函数只重置内存池指针状态，不会主动释放旧内存；正常使用应在池创建后初始化一次。
     */
    void MemoryPool::init(int slotSize)
    {
        assert(slotSize > 0 && "slotSize must be positive");

        // 保证一个 block 至少能容纳：块头 + 最坏对齐填充 + 1 个槽位。
        // 否则像 slotSize=4096 且 BlockSize_=4096 时会出现越界风险。
        // 根据内存对齐原则，slot0的起始地址必须是自身内存大小的整数倍
        const int minBlockBytes = sizeof(Node *) + (slotSize - 1) + slotSize;

        if (BlockSize_ < minBlockBytes)
        {
            BlockSize_ = minBlockBytes;
        }

        SlotSize_ = slotSize;
        firstBlock_ = nullptr;
        curSlot_ = nullptr;
        freeList_ = nullptr;
        lastSlot_ = nullptr;
    }

    /**
     * @brief 分配一个槽位。
     *
     * @return 可用槽位地址；若系统分配失败会抛异常（由 operator new 行为决定）。
     *
     * @details
     * 分配路径分两级：
     * 1. 先尝试从 freeList_ 复用已回收槽位（无锁 CAS）。
     * 2. 若 freeList_ 为空，则从当前大块线性切分；不足时申请新大块。
     */
    void *MemoryPool::allocate()
    {
        // 先尝试复用回收链表中的槽位，减少系统分配开销。
        Node *recycled = popFreeList();
        if (recycled != nullptr)
        {
            return recycled;
        }

        // 线性切分路径需要修改 curSlot_/lastSlot_，用互斥锁保护。
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ == nullptr || curSlot_ >= lastSlot_)
        {
            // 当前块已用尽（或尚未初始化块），触发扩容。
            allocateNewBlock();
        }

        // 取出当前位置作为返回值，然后游标前移一个槽位大小。
        Node *result = curSlot_;
        // 这里先转char*是方便计算移动的距离
        curSlot_ = reinterpret_cast<Node *>(reinterpret_cast<char *>(curSlot_) + SlotSize_);
        return result;
    }

    /**
     * @brief 释放一个槽位到空闲链表。
     *
     * @param ptr 由 allocate() 返回的指针；允许传入 nullptr。
     *
     * @details
     * 这里只负责将节点压回 freeList_，不做真正系统层面的释放。
     */
    void MemoryPool::deallocate(void *ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        pushFreeList(static_cast<Node *>(ptr));
    }

    /**
     * @brief 申请新的大块内存并建立当前块的槽位边界。
     *
     * @details
     * - 头部保留一个指针位，用于把新块挂到 firstBlock_ 链表。
     * - body 之后按 SlotSize_ 对齐，作为第一个可分配槽位地址。
     * - lastSlot_ 表示当前块中“最后一个可起始分配槽位”的边界。
     */
    void MemoryPool::allocateNewBlock()
    {
        // 向系统申请一整块原始内存。
        void *newBlock = ::operator new(BlockSize_);

        // 头插到 block 链表，便于析构时统一回收。
        Node *newBlockHead = static_cast<Node *>(newBlock);
        newBlockHead->next = firstBlock_;
        firstBlock_ = newBlockHead;

        // 预留头部指针空间后，计算对齐并定位第一个可用槽位。
        char *body = static_cast<char *>(newBlock) + sizeof(Node *);
        size_t paddingSize = padPointer(body, SlotSize_);
        curSlot_ = reinterpret_cast<Node *>(body + paddingSize);

        // lastSlot_ 之后将无法再放下一个完整槽位，因此需要扩容。
        lastSlot_ = reinterpret_cast<Node *>(
            reinterpret_cast<size_t>(newBlock) + BlockSize_ - SlotSize_ + 1);
    }

    /**
     * @brief 计算指针按 align 对齐所需补齐字节数。
     *
     * @param p 原始地址。
     * @param align 对齐值（字节）。
     * @return 需要补齐的字节数；若已对齐返回 0。
     */
    size_t MemoryPool::padPointer(char *p, size_t align)
    {
        size_t rem = reinterpret_cast<size_t>(p) % align;
        return rem == 0 ? 0 : (align - rem);
    }

    /**
     * @brief 将节点压入 freeList_（加锁头插）。
     *
     * @param node 待回收节点。
     *
     * @details
     * 使用 mutex 替代原先的 CAS 操作，从根本上避免 ABA 问题，
     * 保证跨线程分配/释放的正确性。
     */
    void MemoryPool::pushFreeList(Node *node)
    {
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        node->next = freeList_;
        freeList_ = node;
    }

    /**
     * @brief 从 freeList_ 弹出一个节点（加锁弹出）。
     *
     * @return 弹出的节点；若链表为空返回 nullptr。
     *
     * @details
     * 使用 mutex 替代原先的 CAS 操作，从根本上避免 ABA 问题，
     * 保证跨线程分配/释放的正确性。
     */
    Node *MemoryPool::popFreeList()
    {
        std::lock_guard<std::mutex> lock(mutexForFreeList_);
        if (freeList_ == nullptr)
        {
            return nullptr;
        }
        Node *head = freeList_;
        freeList_ = head->next;
        return head;
    }

    /**
     * @brief 返回全局槽位列表（按升序存放）。
     */
    std::vector<int> &HashBucket::getSlotSizes()
    {
        static std::vector<int> slotSizes;
        return slotSizes;
    }

    /**
     * @brief 返回全局内存池对象列表。
     */
    std::vector<std::unique_ptr<MemoryPool>> &HashBucket::getPools()
    {
        static std::vector<std::unique_ptr<MemoryPool>> pools;
        return pools;
    }

    /**
     * @brief 返回桶表读写互斥锁。
     */
    std::mutex &HashBucket::getBucketMutex()
    {
        static std::mutex bucketMutex;
        return bucketMutex;
    }

    /**
     * @brief 按用户给定槽位大小初始化/追加内存池集合。
     *
     * @param slotSizes 槽位大小列表（可无序、可重复），必须为正数。
     *
     * @details
     * - 会过滤非正数槽位。
     * - 会自动排序并去重。
     * - 仅为新增尺寸追加新桶，不重建已有桶。
     */
    void HashBucket::initMemoryPool(std::initializer_list<int> slotSizes)
    {
        std::vector<int> normalized = NormalizeSlotSizes(slotSizes);
        if (normalized.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(getBucketMutex());
        std::vector<int> &storedSizes = getSlotSizes();
        std::vector<std::unique_ptr<MemoryPool>> &storedPools = getPools();

        for (int slotSize : normalized)
        {
            std::vector<int>::iterator it = std::lower_bound(storedSizes.begin(), storedSizes.end(), slotSize);
            if (it != storedSizes.end() && *it == slotSize)
            {
                continue;
            }

            size_t index = static_cast<size_t>(std::distance(storedSizes.begin(), it));
            std::unique_ptr<MemoryPool> pool(new MemoryPool());
            pool->init(slotSize);

            storedSizes.insert(storedSizes.begin() + static_cast<std::ptrdiff_t>(index), slotSize);
            storedPools.insert(storedPools.begin() + static_cast<std::ptrdiff_t>(index), std::move(pool));
        }
    }

    /**
     * @brief 获取指定下标对应的内存池实例。
     */
    MemoryPool &HashBucket::getMemoryPool(int index)
    {
        MemoryPool *pool = nullptr;
        {
            std::lock_guard<std::mutex> lock(getBucketMutex());
            std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();
            assert(index >= 0 && index < static_cast<int>(pools.size()));
            pool = pools[static_cast<size_t>(index)].get();
        }
        assert(pool != nullptr);
        return *pool;
    }

    /**
     * @brief 按请求大小从配置槽位中选择最小可容纳桶进行分配。
     *
     * @param size 请求大小（字节），必须为正数。
     * @return 分配地址；若没有可容纳桶，退化为 operator new。
     */
    void *HashBucket::useMemory(int size)
    {
        if (size <= 0)
        {
            return nullptr;
        }

        MemoryPool *pool = nullptr;
        {
            std::lock_guard<std::mutex> lock(getBucketMutex());
            const std::vector<int> &slotSizes = getSlotSizes();
            const std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();
            if (!slotSizes.empty() && !pools.empty())
            {
                std::vector<int>::const_iterator it = std::lower_bound(slotSizes.begin(), slotSizes.end(), size);
                if (it != slotSizes.end())
                {
                    size_t index = static_cast<size_t>(std::distance(slotSizes.begin(), it));
                    pool = pools[index].get();
                }
            }
        }

        if (pool != nullptr)
        {
            return pool->allocate();
        }

        void *ptr = ::operator new(static_cast<size_t>(size));
        {
            std::lock_guard<std::mutex> fallbackLock(GetFallbackMutex());
            GetFallbackAllocations().insert(ptr);
        }
        return ptr;
    }

    /**
     * @brief 按原始申请大小定位桶并释放。
     *
     * @param ptr 待释放指针。
     * @param size 原始申请大小（字节），必须为正数。
     */
    void HashBucket::freeMemory(void *ptr, int size)
    {
        if (ptr == nullptr || size <= 0)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> fallbackLock(GetFallbackMutex());
            std::unordered_set<void *> &fallbackAllocations = GetFallbackAllocations();
            std::unordered_set<void *>::iterator fallbackIt = fallbackAllocations.find(ptr);
            if (fallbackIt != fallbackAllocations.end())
            {
                fallbackAllocations.erase(fallbackIt);
                ::operator delete(ptr);
                return;
            }
        }

        MemoryPool *pool = nullptr;
        {
            std::lock_guard<std::mutex> lock(getBucketMutex());
            const std::vector<int> &slotSizes = getSlotSizes();
            const std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();
            if (!slotSizes.empty() && !pools.empty())
            {
                std::vector<int>::const_iterator it = std::lower_bound(slotSizes.begin(), slotSizes.end(), size);
                if (it != slotSizes.end())
                {
                    size_t index = static_cast<size_t>(std::distance(slotSizes.begin(), it));
                    pool = pools[index].get();
                }
            }
        }

        if (pool != nullptr)
        {
            pool->deallocate(ptr);
            return;
        }

        ::operator delete(ptr);
    }

} // namespace base

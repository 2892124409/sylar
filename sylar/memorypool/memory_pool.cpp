#include "memory_pool.h"

#include <algorithm>

namespace sylar
{

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
     * @brief 返回初始化互斥锁。
     */
    std::mutex &HashBucket::getInitMutex()
    {
        static std::mutex initMutex;
        return initMutex;
    }

    /**
     * @brief 返回初始化状态标志。
     */
    bool &HashBucket::getInitializedFlag()
    {
        // 函数内静态变量，只创建一次，生命周期到程序结束，所有地方拿到的都是同一个标记
        static bool initialized = false;
        return initialized;
    }

    /**
     * @brief 按用户给定槽位大小初始化内存池集合。
     *
     * @param slotSizes 槽位大小列表（可无序、可重复），必须为正数。
     *
     * @details
     * - 会过滤非正数槽位。
     * - 会自动排序并去重。
     * - 为避免运行期重建导致野指针风险，仅允许初始化一次。
     */
    void HashBucket::initMemoryPool(std::initializer_list<int> slotSizes)
    {
        // 加锁防止多线程并发初始化
        // lock_guard 在作用域结束时自动解锁
        std::lock_guard<std::mutex> lock(getInitMutex());

        // 获取初始化标志的引用（引用才能修改原值）
        bool &initialized = getInitializedFlag();

        // 如果已经初始化过，直接返回，避免重复初始化
        // 重复初始化会导致旧的 MemoryPool 被销毁，可能产生野指针
        if (initialized)
        {
            return;
        }

        // 将 initializer_list 复制到 vector 中，方便后续处理
        std::vector<int> normalized(slotSizes.begin(), slotSizes.end());

        // 过滤掉非正数（<=0 的值）
        // remove_if 把满足条件的元素移到末尾，返回新的逻辑末尾迭代器
        // erase 真正删除这些元素
        normalized.erase(
            std::remove_if(normalized.begin(), normalized.end(),
                           [](int size)
                           { return size <= 0; }),
            normalized.end());

        // 按升序排序，为后续 lower_bound 二分查找做准备
        std::sort(normalized.begin(), normalized.end());

        // 去重：unique 把重复元素移到末尾，返回新的逻辑末尾迭代器
        // erase 真正删除重复元素
        // 例如：[64, 64, 128, 128, 256] → [64, 128, 256]
        normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());

        // 如果过滤后列表为空（用户传入的全是非正数），直接返回
        if (normalized.empty())
        {
            return;
        }

        // 获取全局存储的槽位大小列表和内存池列表
        std::vector<int> &storedSizes = getSlotSizes();
        std::vector<std::unique_ptr<MemoryPool>> &storedPools = getPools();

        // 保存处理后的槽位大小列表
        storedSizes = normalized;

        // 清空旧的内存池（如果有），准备创建新的
        storedPools.clear();
        // 预分配空间，避免多次扩容
        storedPools.reserve(storedSizes.size());

        // 为每个槽位大小创建一个对应的 MemoryPool
        for (int slotSize : storedSizes)
        {
            // C++11 环境下没有 std::make_unique，这里直接 new
            std::unique_ptr<MemoryPool> pool(new MemoryPool());
            // 初始化内存池的槽位大小
            pool->init(slotSize);
            // 将内存池移动到列表中（unique_ptr 不能拷贝，只能移动）
            storedPools.emplace_back(std::move(pool));
        }

        // 标记初始化完成，后续调用将直接返回
        initialized = true;
    }

    /**
     * @brief 获取指定下标对应的内存池实例。
     */
    MemoryPool &HashBucket::getMemoryPool(int index)
    {
        std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();
        assert(index >= 0 && index < static_cast<int>(pools.size()));
        return *pools[static_cast<size_t>(index)];
    }

    /**
     * @brief 按请求大小从配置槽位中选择最小可容纳桶进行分配。
     *
     * @param size 请求大小（字节），必须为正数。
     * @return 分配地址；若没有可容纳桶，退化为 operator new。
     */
    void *HashBucket::useMemory(int size)
    {
        // 参数校验：非正数请求直接返回空指针
        if (size <= 0)
        {
            return nullptr;
        }

        // 获取全局槽位大小列表（已按升序排列）
        // 例如：[64, 128, 256, 512, 1024]
        const std::vector<int> &slotSizes = getSlotSizes();
        // 获取全局内存池列表，每个池对应 slotSizes 中的一个槽位大小
        const std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();

        // 如果尚未初始化（用户未调用 initMemoryPool），退化使用系统 new
        if (slotSizes.empty() || pools.empty())
        {
            return ::operator new(static_cast<size_t>(size));
        }

        // 二分查找：找到第一个 >= size 的槽位大小
        // 例如：size=100，slotSizes=[64,128,256]，则找到 128
        // lower_bound 返回指向该元素的迭代器
        auto it = std::lower_bound(slotSizes.begin(), slotSizes.end(), size);

        // 如果迭代器到达末尾，说明所有槽位都 < size
        // 请求太大，没有合适的桶，退化使用系统 new
        if (it == slotSizes.end())
        {
            return ::operator new(static_cast<size_t>(size));
        }

        // 计算找到的槽位在 vector 中的下标
        // distance 返回从 begin 到 it 的元素个数
        size_t index = static_cast<size_t>(std::distance(slotSizes.begin(), it));

        // 从对应的内存池中分配一个槽位并返回
        return pools[index]->allocate();
    }

    /**
     * @brief 按原始申请大小定位桶并释放。
     *
     * @param ptr 待释放指针。
     * @param size 原始申请大小（字节），必须为正数。
     */
    void HashBucket::freeMemory(void *ptr, int size)
    {
        // 参数校验：空指针或非正数 size 直接返回，不做任何操作
        if (ptr == nullptr || size <= 0)
        {
            return;
        }

        // 获取全局槽位大小列表（已按升序排列）
        const std::vector<int> &slotSizes = getSlotSizes();
        // 获取全局内存池列表
        const std::vector<std::unique_ptr<MemoryPool>> &pools = getPools();

        // 如果尚未初始化，说明当初是用系统 new 分配的，用系统 delete 释放
        if (slotSizes.empty() || pools.empty())
        {
            ::operator delete(ptr);
            return;
        }

        // 二分查找：找到当初分配时使用的槽位大小
        // 必须与 useMemory 中的查找逻辑一致，确保释放到正确的池
        auto it = std::lower_bound(slotSizes.begin(), slotSizes.end(), size);

        // 如果迭代器到达末尾，说明当初是用系统 new 分配的（请求太大）
        // 用系统 delete 释放
        if (it == slotSizes.end())
        {
            ::operator delete(ptr);
            return;
        }

        // 计算槽位在 vector 中的下标，找到对应的内存池
        size_t index = static_cast<size_t>(std::distance(slotSizes.begin(), it));

        // 将内存归还到对应的内存池（实际上是挂回 freeList_）
        pools[index]->deallocate(ptr);
    }

} // namespace sylar

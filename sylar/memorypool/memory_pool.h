#ifndef SYLAR_MEMORY_POOL_H
#define SYLAR_MEMORY_POOL_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace sylar
{

    /**
     * @brief 链表节点，可指向下一个可复用槽位；也可指向下一个大块内存。
     *
     * @note Node 只包含了一个指针，所以 sizeof 不是用户实际可用槽位大小。
     */
    struct Node
    {
        Node *next{nullptr}; /**< 指向链表下一个节点。 */
    };

    /**
     * @brief 单一尺寸类别的定长槽内存池。
     */
    class MemoryPool
    {
    public:
        /**
         * @brief 构造内存池。
         * @param blockSize 每次向系统申请的大块内存大小（字节），必须为正数。
         */
        explicit MemoryPool(int blockSize = 4096);

        /**
         * @brief 析构内存池并释放所有已申请的大块内存。
         */
        ~MemoryPool();

        /**
         * @brief 初始化当前内存池的槽位大小。
         * @param slotSize 槽位大小（字节），必须为正数。
         */
        void init(int slotSize);

        /**
         * @brief 分配一个槽位。
         * @return 返回分配到的内存地址；失败返回 nullptr。
         */
        void *allocate();

        /**
         * @brief 释放一个槽位到内存池。
         * @param ptr 由 allocate() 返回的内存地址。
         */
        void deallocate(void *ptr);

    private:
        /**
         * @brief 申请并挂接一个新的大块内存。
         */
        void allocateNewBlock();

        /**
         * @brief 计算指针对齐所需的填充字节数。
         * @param p 原始指针。
         * @param align 对齐粒度（字节）。
         * @return 需要补齐的字节数。
         */
        size_t padPointer(char *p, size_t align);

        /**
         * @brief 将节点压入空闲链表（加锁保护）。
         * @param node 待压入节点。
         */
        void pushFreeList(Node *node);

        /**
         * @brief 从空闲链表弹出节点（加锁保护）。
         * @return 返回链表头节点；若为空返回 nullptr。
         */
        Node *popFreeList();

    private:
        int BlockSize_;                /**< 向系统申请的单个内存块大小。 */
        int SlotSize_;                 /**< 当前内存池管理的槽位大小。 */
        Node *firstBlock_;             /**< 已申请内存块链表头。 */
        Node *curSlot_;                /**< 当前尚未使用过的槽位游标。 */
        Node *freeList_;               /**< 回收槽位链表头。 */
        Node *lastSlot_;               /**< 当前块内可分配槽位的末尾边界。 */
        std::mutex mutexForBlock_;     /**< 保护扩容路径的互斥锁。 */
        std::mutex mutexForFreeList_;  /**< 保护空闲链表的互斥锁，解决跨线程 ABA 问题。 */
    };

    /**
     * @brief 桶路由器：按大小将请求分发到指定的 MemoryPool。
     *
     * @details
     * 通过 initMemoryPool(...) 传入若干槽位大小后，系统会按升序保存。
     * 分配时采用 lower_bound 选择第一个 >= 请求大小的槽位池。
     */
    class HashBucket
    {
    public:
        /**
         * @brief 使用初始化列表初始化桶内存池。
         * @param slotSizes 槽位大小列表，例如 {32, 4096}，必须为正数。
         */
        static void initMemoryPool(std::initializer_list<int> slotSizes);

        /**
         * @brief 使用可变参数初始化桶内存池。
         * @tparam Sizes 整型参数类型。
         * @param slotSizes 槽位大小参数，例如 initMemoryPool(32, 4096)，必须为正数。
         */
        // std::enable_if_t和std::is_integral_v<Sizes>配合确保参数必须是整数类型
        //&& ..表示对可变参数展开，就是对所有参数进行整数判断
        template <typename... Sizes,
                  typename = std::enable_if_t<(std::is_integral_v<Sizes> && ...)>>
        static void initMemoryPool(Sizes... slotSizes)
        {
            static_assert(sizeof...(slotSizes) > 0,
                          "initMemoryPool 至少需要一个槽位大小参数");
            initMemoryPool({static_cast<int>(slotSizes)...});
        }

        /**
         * @brief 获取指定下标对应的内存池。
         * @param index 内存池下标。
         * @return 对应 MemoryPool 的引用。
         */
        static MemoryPool &getMemoryPool(int index);

        /**
         * @brief 通过桶路由分配内存。
         * @param size 请求大小（字节），必须为正数。
         * @return 分配结果指针；当 size 为 0 时返回 nullptr。
         */
        static void *useMemory(int size);

        /**
         * @brief 通过桶路由释放内存。
         * @param ptr 待释放指针。
         * @param size 原始申请大小（字节），必须为正数。
         */
        static void freeMemory(void *ptr, int size);

        template <typename T, typename... Args>
        friend T *newElement(Args &&...args);

        template <typename T>
        friend void deleteElement(T *p);

    private:
        /**
         * @brief 获取槽位大小列表（升序）。
         */
        static std::vector<int> &getSlotSizes();

        /**
         * @brief 获取对应的内存池对象列表。
         */
        static std::vector<std::unique_ptr<MemoryPool>> &getPools();

        /**
         * @brief 获取初始化互斥锁。
         */
        static std::mutex &getInitMutex();

        /**
         * @brief 获取初始化标记。
         */
        static bool &getInitializedFlag();
    };

    /**
     * @brief 从内存池分配内存并原地构造对象。
     * @tparam T 对象类型。
     * @tparam Args 构造参数类型。
     * @param args 构造参数。
     * @return 构造好的对象指针；分配失败返回 nullptr。
     */
    template <typename T, typename... Args>
    T *newElement(Args &&...args)
    {
        T *p = static_cast<T *>(HashBucket::useMemory(sizeof(T)));
        if (p != nullptr)
        {
            new (p) T(std::forward<Args>(args)...);
        }
        return p;
    }

    /**
     * @brief 析构对象并将内存归还给内存池。
     * @tparam T 对象类型。
     * @param p 对象指针。
     */
    template <typename T>
    void deleteElement(T *p)
    {
        if (p != nullptr)
        {
            p->~T();
            HashBucket::freeMemory(static_cast<void *>(p), sizeof(T));
        }
    }

} // namespace sylar

#endif // SYLAR_MEMORY_POOL_H

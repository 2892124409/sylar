#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

/**
 * @brief 定长对象内存池（线程本地、无锁）
 *
 * @tparam T         池中管理的对象类型
 * @tparam BlockSize 每次向系统申请的内存块大小（字节），默认 4096
 *
 * 设计思路：
 *   内存池维护一个"大块链表"，每个大块被切分为若干等大的槽位（slot）。
 *   分配时优先从回收链表（freeSlots_）取槽位，其次从当前块线性切分。
 *   释放时只把槽位挂回 freeSlots_，不归还给系统，下次分配直接复用。
 *   析构时统一释放所有大块。
 *
 * 符合 C++ 标准分配器（Allocator）接口，可直接用于 STL 容器。
 *
 * 线程模型说明：
 *   该实现假设每个 MemoryPool 实例只在单线程中使用（推荐通过 thread_local 持有）。
 *   因此 allocate/deallocate 路径不加锁，追求最短延迟。
 */
template <typename T, size_t BlockSize = 4096>
class MemoryPool
{
  public:
    // =========================================================================
    // 标准分配器要求的类型别名
    // =========================================================================
    typedef T               value_type;       // 管理的对象类型
    typedef T*              pointer;          // 对象指针
    typedef T&              reference;        // 对象引用
    typedef const T*        const_pointer;    // const 对象指针
    typedef const T&        const_reference;  // const 对象引用
    typedef size_t          size_type;        // 大小类型
    typedef ptrdiff_t       difference_type;  // 指针差值类型

    // 拷贝赋值时不传播分配器（各容器独立持有自己的池）
    typedef std::false_type propagate_on_container_copy_assignment;
    // 移动赋值和 swap 时传播分配器（转移所有权）
    typedef std::true_type  propagate_on_container_move_assignment;
    typedef std::true_type  propagate_on_container_swap;

    /**
     * @brief rebind 机制：允许容器将分配器重绑定到另一个类型
     * @details 例如 std::list<T, MemoryPool<T>> 内部需要分配 list_node<T>，
     *          通过 rebind<list_node<T>>::other 得到对应的分配器类型。
     */
    template <typename U> struct rebind {
      typedef MemoryPool<U> other;
    };

    // =========================================================================
    // 构造 / 析构
    // =========================================================================

    /** @brief 默认构造，初始化所有指针为空 */
    MemoryPool() noexcept;

    /** @brief 拷贝构造：不复制内存块，创建一个全新的空池 */
    MemoryPool(const MemoryPool& memoryPool) noexcept;

    /** @brief 移动构造：接管源池的所有内存块，源池置空 */
    MemoryPool(MemoryPool&& memoryPool) noexcept;

    /**
     * @brief 跨类型拷贝构造（rebind 场景使用）
     * @details 同样创建空池，不复制源池数据
     */
    template <class U> MemoryPool(const MemoryPool<U>& memoryPool) noexcept;

    /** @brief 析构：遍历大块链表，逐一释放所有向系统申请的内存 */
    ~MemoryPool() noexcept;

    /** @brief 禁止拷贝赋值，避免两个池共享同一块内存导致双重释放 */
    MemoryPool& operator=(const MemoryPool& memoryPool) = delete;

    /** @brief 移动赋值：交换两个池的内部状态 */
    MemoryPool& operator=(MemoryPool&& memoryPool) noexcept;

    // =========================================================================
    // 标准分配器接口
    // =========================================================================

    /** @brief 返回对象 x 的地址（标准分配器要求） */
    pointer address(reference x) const noexcept;
    const_pointer address(const_reference x) const noexcept;

    /**
     * @brief 分配一个对象槽位（不构造对象）
     * @param n    忽略，内存池每次只分配一个对象
     * @param hint 忽略
     * @return 指向可用内存的指针
     */
    pointer allocate(size_type n = 1, const_pointer hint = 0);

    /**
     * @brief 释放一个对象槽位（不析构对象）
     * @param p 由 allocate() 返回的指针
     * @param n 忽略
     */
    void deallocate(pointer p, size_type n = 1);

    /** @brief 返回理论上可分配的最大对象数量 */
    size_type max_size() const noexcept;

    /**
     * @brief 在已分配的内存上原地构造对象（placement new）
     * @param p    目标内存地址
     * @param args 转发给构造函数的参数
     */
    template <class U, class... Args> void construct(U* p, Args&&... args);

    /**
     * @brief 显式调用对象析构函数（不释放内存）
     * @param p 目标对象指针
     */
    template <class U> void destroy(U* p);

    /**
     * @brief 分配内存并构造对象（allocate + construct 的组合）
     * @param args 转发给构造函数的参数
     * @return 构造好的对象指针
     */
    template <class... Args> pointer newElement(Args&&... args);

    /**
     * @brief 析构对象并释放内存回池（destroy + deallocate 的组合）
     * @param p 由 newElement() 返回的指针
     */
    void deleteElement(pointer p);

  private:
    // =========================================================================
    // 内部数据结构
    // =========================================================================

    /**
     * @brief 槽位联合体
     * @details 一个槽位在两种状态下复用同一块内存：
     *   - 已分配：存放用户对象 element
     *   - 空闲中：作为链表节点，next 指向下一个空闲槽位
     */
    union Slot_ {
      value_type element;  // 用户对象
      Slot_*     next;     // 空闲链表指针
    };

    typedef char*  data_pointer_;  // 原始字节指针，用于指针算术
    typedef Slot_  slot_type_;     // 槽位类型
    typedef Slot_* slot_pointer_;  // 槽位指针

    // =========================================================================
    // 成员变量
    // =========================================================================

    /** @brief 大块链表头：所有向系统申请的内存块通过 next 串联，析构时统一释放 */
    slot_pointer_ currentBlock_;

    /** @brief 当前块中下一个可线性切分的槽位位置（游标） */
    slot_pointer_ currentSlot_;

    /**
     * @brief 当前块的末尾边界
     * @details currentSlot_ >= lastSlot_ 时说明当前块已用尽，需要申请新块
     */
    slot_pointer_ lastSlot_;

    /** @brief 回收槽位链表头：deallocate() 归还的槽位挂在这里，allocate() 优先复用（单线程无锁） */
    slot_pointer_ freeSlots_;

    // =========================================================================
    // 私有辅助函数
    // =========================================================================

    /**
     * @brief 计算指针 p 按 align 对齐所需的填充字节数
     * @param p     原始指针
     * @param align 对齐粒度（字节）
     * @return 需要补齐的字节数；已对齐则返回 0
     */
    size_type padPointer(data_pointer_ p, size_type align) const noexcept;

    /**
     * @brief 向系统申请一个新的大块内存，并更新 currentSlot_ / lastSlot_
     * @details 大块布局：
     *   [ slot_pointer_ next ][ padding ][ slot0 ][ slot1 ] ... [ slotN ]
     *   头部存放指向上一个大块的指针，body 部分按槽位对齐后线性切分。
     */
    void allocateBlock();

    // BlockSize 至少要能放下 2 个槽位，否则无意义
    static_assert(BlockSize >= 2 * sizeof(slot_type_), "BlockSize too small.");
};

// =============================================================================
// 模板实现
// =============================================================================

/**
 * @brief 计算对齐填充字节数
 * @details 将指针转为整数后取模，得到距下一个对齐边界的偏移量
 */
template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::size_type
MemoryPool<T, BlockSize>::padPointer(data_pointer_ p, size_type align)
const noexcept
{
  uintptr_t result = reinterpret_cast<uintptr_t>(p);
  return ((align - result) % align);
}

/** @brief 默认构造：所有指针置空，池处于未初始化状态 */
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool()
noexcept
{
  currentBlock_ = nullptr;
  currentSlot_  = nullptr;
  lastSlot_     = nullptr;
  freeSlots_    = nullptr;
}

/** @brief 拷贝构造：创建空池，不复制源池的内存块 */
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool(const MemoryPool& memoryPool)
noexcept :
MemoryPool()
{}

/**
 * @brief 移动构造：接管源池所有内存块
 * @details 将源池的 currentBlock_ 置空，防止析构时重复释放
 */
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool(MemoryPool&& memoryPool)
noexcept
{
  currentBlock_            = memoryPool.currentBlock_;
  memoryPool.currentBlock_ = nullptr;  // 源池放弃所有权
  currentSlot_             = memoryPool.currentSlot_;
  lastSlot_                = memoryPool.lastSlot_;
  freeSlots_               = memoryPool.freeSlots_;
  memoryPool.currentSlot_  = nullptr;
  memoryPool.lastSlot_     = nullptr;
  memoryPool.freeSlots_    = nullptr;
}

/** @brief 跨类型拷贝构造（rebind 场景）：创建空池 */
template <typename T, size_t BlockSize>
template<class U>
MemoryPool<T, BlockSize>::MemoryPool(const MemoryPool<U>& memoryPool)
noexcept :
MemoryPool()
{}

/**
 * @brief 移动赋值：交换两个池的内部状态
 * @details 用 swap 交换 currentBlock_ 保证原来的内存块在 memoryPool 析构时被释放
 */
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>&
MemoryPool<T, BlockSize>::operator=(MemoryPool&& memoryPool)
noexcept
{
  if (this != &memoryPool)
  {
    std::swap(currentBlock_, memoryPool.currentBlock_);
    std::swap(currentSlot_, memoryPool.currentSlot_);
    std::swap(lastSlot_, memoryPool.lastSlot_);
    std::swap(freeSlots_, memoryPool.freeSlots_);
  }
  return *this;
}

/**
 * @brief 析构：遍历大块链表，逐一调用 operator delete 释放所有内存
 * @details freeSlots_ 链表中的节点都在大块内，随大块一起释放，无需单独处理
 */
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::~MemoryPool()
noexcept
{
  slot_pointer_ curr = currentBlock_;
  while (curr != nullptr) {
    slot_pointer_ prev = curr->next;           // 先保存下一个大块地址
    operator delete(reinterpret_cast<void*>(curr));  // 释放当前大块
    curr = prev;
  }
}

/** @brief 返回对象地址（标准分配器接口，直接取地址即可） */
template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::address(reference x)
const noexcept
{
  return &x;
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::const_pointer
MemoryPool<T, BlockSize>::address(const_reference x)
const noexcept
{
  return &x;
}

/**
 * @brief 向系统申请新的大块内存
 * @details 大块内存布局（从低地址到高地址）：
 *   [  next 指针（sizeof slot_pointer_）  ]
 *   [  对齐填充（0 ~ alignof(slot_type_)-1 字节）  ]
 *   [  slot0  ][  slot1  ] ... [  slotN  ]
 *
 *   头部的 next 指针将新块头插到 currentBlock_ 链表，析构时按链表顺序释放。
 *   lastSlot_ 指向最后一个可起始分配的槽位边界（不是末尾，而是"再往后就放不下一个完整槽"的位置）。
 */
template <typename T, size_t BlockSize>
void
MemoryPool<T, BlockSize>::allocateBlock()
{
  // 向系统申请一整块原始内存
  data_pointer_ newBlock = reinterpret_cast<data_pointer_>(operator new(BlockSize));

  // 头插到大块链表，析构时统一回收
  reinterpret_cast<slot_pointer_>(newBlock)->next = currentBlock_;
  currentBlock_ = reinterpret_cast<slot_pointer_>(newBlock);

  // 跳过头部的 next 指针，计算 body 起始地址
  data_pointer_ body = newBlock + sizeof(slot_pointer_);

  // 计算对齐填充，确保第一个槽位地址满足 slot_type_ 的对齐要求
  size_type bodyPadding = padPointer(body, alignof(slot_type_));
  currentSlot_ = reinterpret_cast<slot_pointer_>(body + bodyPadding);

  // lastSlot_：块尾减去一个槽位大小再加 1，即"最后一个合法槽位起始地址 + 1"
  // 当 currentSlot_ >= lastSlot_ 时说明当前块已无法再放下一个完整槽位
  lastSlot_ = reinterpret_cast<slot_pointer_>(newBlock + BlockSize - sizeof(slot_type_) + 1);
}

/**
 * @brief 分配一个槽位（不构造对象）
 * @details 分配优先级：
 *   1. 优先从 freeSlots_ 回收链表取槽位
 *   2. 从当前块线性切分
 *   3. 当前块用尽时申请新块，再切分
 *   本实现无锁，要求每个池实例只在单线程使用（推荐 thread_local）。
 */
template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::allocate(size_type n, const_pointer hint)
{
  // 优先复用回收槽位
  slot_pointer_ head = freeSlots_;
  if (head != nullptr) {
    freeSlots_ = head->next;
    return reinterpret_cast<pointer>(head);
  }

  // 线性切分
  if (currentSlot_ == nullptr || currentSlot_ >= lastSlot_)
    allocateBlock();
  pointer result = reinterpret_cast<pointer>(currentSlot_++);
  return result;
}

/**
 * @brief 释放一个槽位（不析构对象）
 * @details 将槽位头插到 freeSlots_ 链表。
 *          内存不归还给系统，池的总内存只增不减。
 */
template <typename T, size_t BlockSize>
inline void
MemoryPool<T, BlockSize>::deallocate(pointer p, size_type n)
{
  if (p != nullptr) {
    slot_pointer_ node = reinterpret_cast<slot_pointer_>(p);
    node->next = freeSlots_;
    freeSlots_ = node;
  }
}

/**
 * @brief 返回理论最大可分配对象数
 * @details 用地址空间上限除以块大小得到最大块数，再乘以每块的槽位数
 */
template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::size_type
MemoryPool<T, BlockSize>::max_size()
const noexcept
{
  size_type maxBlocks = -1 / BlockSize;
  return (BlockSize - sizeof(data_pointer_)) / sizeof(slot_type_) * maxBlocks;
}

/**
 * @brief 在已分配的内存 p 上原地构造类型 U 的对象（placement new）
 * @param p    目标内存，必须已由 allocate() 分配
 * @param args 完美转发给 U 的构造函数
 */
template <typename T, size_t BlockSize>
template <class U, class... Args>
inline void
MemoryPool<T, BlockSize>::construct(U* p, Args&&... args)
{
  new (p) U (std::forward<Args>(args)...);
}

/**
 * @brief 显式调用对象析构函数（不释放内存）
 * @param p 目标对象指针
 */
template <typename T, size_t BlockSize>
template <class U>
inline void
MemoryPool<T, BlockSize>::destroy(U* p)
{
  p->~U();
}

/**
 * @brief 分配内存并构造对象（allocate + construct 的便捷组合）
 * @param args 完美转发给 T 的构造函数
 * @return 构造好的对象指针
 */
template <typename T, size_t BlockSize>
template <class... Args>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::newElement(Args&&... args)
{
  pointer result = allocate();
  construct<value_type>(result, std::forward<Args>(args)...);
  return result;
}

/**
 * @brief 析构对象并将内存归还给池（destroy + deallocate 的便捷组合）
 * @param p 由 newElement() 返回的指针
 */
template <typename T, size_t BlockSize>
inline void
MemoryPool<T, BlockSize>::deleteElement(pointer p)
{
  if (p != nullptr) {
    p->~value_type();
    deallocate(p);
  }
}

/**
 * @brief 获取线程本地内存池实例
 * @details 每个线程持有一份独立池实例，分配/释放路径完全无锁。
 */
template <typename T, size_t BlockSize = 4096>
inline MemoryPool<T, BlockSize>& getThreadLocalPool()
{
  static thread_local MemoryPool<T, BlockSize> pool;
  return pool;
}

#endif // MEMORY_POOL_H

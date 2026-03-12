#ifndef __SYLAR_HTTP_MEMORY_POOL_H__
#define __SYLAR_HTTP_MEMORY_POOL_H__

#include "memorypool/memory_pool.h"

#include <memory>
#include <utility>

namespace http
{

    /**
     * @brief 确保 HTTP 层常用对象尺寸已注册到全局 HashBucket。
     * @details
     * 当前只注册 HTTP 框架第一批高频对象本体尺寸：
     * - HttpRequest
     * - HttpResponse
     * - Session
     * - HttpSession
     */
    void EnsureHttpMemoryPoolsInitialized();

    /**
     * @brief 用全局内存池创建一个 HTTP 层共享对象。
     * @details
     * 该 helper 只负责对象本体的池化：
     * - std::shared_ptr 控制块仍由标准分配器管理
     * - string/map/vector 等成员的内部堆分配保持不变
     */
    template <typename T, typename... Args>
    std::shared_ptr<T> MakeHttpPooledShared(Args &&...args)
    {
        EnsureHttpMemoryPoolsInitialized();
        T *object = sylar::newElement<T>(std::forward<Args>(args)...);
        return std::shared_ptr<T>(object, [](T *ptr)
                                  { sylar::deleteElement(ptr); });
    }

} // namespace http

#endif

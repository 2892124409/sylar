// 头文件保护宏：防止头文件被重复包含。
#ifndef __SYLAR_HTTP_SESSION_STORAGE_H__
// 定义头文件保护宏。
#define __SYLAR_HTTP_SESSION_STORAGE_H__

// 引入 Session 类型定义，供存储接口读写会话对象使用。
#include "http/session/session.h"
// 引入互斥锁，供内存存储实现做并发保护。
#include "sylar/concurrency/mutex/mutex.h"

// 引入 std::unordered_map，作为内存存储的底层容器。
#include <unordered_map>
// 引入 std::shared_ptr 等智能指针能力。
#include <memory>
// 引入 std::string，作为 Session ID 的字符串类型。
#include <string>

// sylar 顶层命名空间。
namespace sylar
{
    // http 子命名空间。
    namespace http
    {

        // Session 存储抽象接口：定义 Session 持久化/读取/删除/过期清理的统一契约。
        class SessionStorage
        {
        public:
            // SessionStorage 智能指针别名，统一代码中的指针写法。
            typedef std::shared_ptr<SessionStorage> ptr;
            // 虚析构函数：保证通过基类指针释放派生类对象时析构完整。
            virtual ~SessionStorage() {}

            // 保存会话：若已存在相同 SID，通常语义为覆盖更新。
            virtual void save(Session::ptr session) = 0;
            // 按 SID 加载会话：未命中时返回空指针。
            virtual Session::ptr load(const std::string &session_id) = 0;
            // 按 SID 删除会话：返回是否删除成功。
            virtual bool remove(const std::string &session_id) = 0;
            // 清理过期会话：传入当前时间（毫秒），返回清理数量。
            virtual size_t sweepExpired(uint64_t now_ms) = 0;
        };

        // 默认内存实现：基于 map 保存 Session，适合单进程场景。
        class MemorySessionStorage : public SessionStorage
        {
        public:
            // MemorySessionStorage 智能指针别名。
            typedef std::shared_ptr<MemorySessionStorage> ptr;

            // 覆盖基类接口：保存会话到内存表。
            virtual void save(Session::ptr session) override;
            // 覆盖基类接口：按 SID 从内存表读取会话。
            virtual Session::ptr load(const std::string &session_id) override;
            // 覆盖基类接口：按 SID 从内存表删除会话。
            virtual bool remove(const std::string &session_id) override;
            // 覆盖基类接口：扫描并清理过期会话。
            virtual size_t sweepExpired(uint64_t now_ms) override;

        private:
            // 互斥锁：保护 m_sessions 的并发读写。
            Mutex m_mutex;
            // 内存会话表：key 为 SID，value 为 Session 对象。
            std::unordered_map<std::string, Session::ptr> m_sessions;
        };

    } // namespace http
} // namespace sylar

// 结束头文件保护宏。
#endif

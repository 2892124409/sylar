// 引入 SessionStorage 接口与内存实现声明。
#include "http/session/session_storage.h"

// sylar 顶层命名空间。
namespace http
{

    // 保存会话到内存表（同 SID 会被覆盖）。
    void MemorySessionStorage::save(Session::ptr session)
    {
        // 对内存会话表加锁，保证并发写安全。
        sylar::Mutex::Lock lock(m_mutex);
        // 以 session id 为 key 保存会话对象。
        m_sessions[session->getId()] = session;
    }

    // 按 SID 从内存表读取会话。
    Session::ptr MemorySessionStorage::load(const std::string &session_id)
    {
        // 对内存会话表加锁，保证并发读一致性。
        sylar::Mutex::Lock lock(m_mutex);
        // 在 map 中查找指定 SID。
        std::unordered_map<std::string, Session::ptr>::iterator it = m_sessions.find(session_id);
        // 命中返回对象，未命中返回空指针。
        return it == m_sessions.end() ? Session::ptr() : it->second;
    }

    // 按 SID 删除会话。
    bool MemorySessionStorage::remove(const std::string &session_id)
    {
        // 删除前加锁，保证并发安全。
        sylar::Mutex::Lock lock(m_mutex);
        // erase 返回删除数量，大于 0 代表删除成功。
        return m_sessions.erase(session_id) > 0;
    }

    // 扫描并清理过期会话，返回清理数量。
    size_t MemorySessionStorage::sweepExpired(uint64_t now_ms)
    {
        // 清理期间加锁，防止迭代器被并发修改破坏。
        sylar::Mutex::Lock lock(m_mutex);
        // 记录本次清理掉的会话数量。
        size_t count = 0;
        // 遍历内存会话表，逐个判断是否过期。
        for (std::unordered_map<std::string, Session::ptr>::iterator it = m_sessions.begin(); it != m_sessions.end();)
        {
            // 若会话已过期，则从 map 删除。
            if (it->second->isExpired(now_ms))
            {
                // erase(it) 返回下一个有效迭代器，避免迭代器失效。
                it = m_sessions.erase(it);
                // 清理计数加一。
                ++count;
            }
            else
            {
                // 未过期则继续检查下一个会话。
                ++it;
            }
        }
        // 返回本轮清理的总数。
        return count;
    }

} // namespace http

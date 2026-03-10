#include "sylar/http/session_storage.h"

namespace sylar
{
    namespace http
    {

        void MemorySessionStorage::save(Session::ptr session)
        {
            Mutex::Lock lock(m_mutex);
            m_sessions[session->getId()] = session;
        }

        Session::ptr MemorySessionStorage::load(const std::string &session_id)
        {
            Mutex::Lock lock(m_mutex);
            std::map<std::string, Session::ptr>::iterator it = m_sessions.find(session_id);
            return it == m_sessions.end() ? Session::ptr() : it->second;
        }

        bool MemorySessionStorage::remove(const std::string &session_id)
        {
            Mutex::Lock lock(m_mutex);
            return m_sessions.erase(session_id) > 0;
        }

        size_t MemorySessionStorage::sweepExpired(uint64_t now_ms)
        {
            Mutex::Lock lock(m_mutex);
            size_t count = 0;
            for (std::map<std::string, Session::ptr>::iterator it = m_sessions.begin(); it != m_sessions.end();)
            {
                if (it->second->isExpired(now_ms))
                {
                    it = m_sessions.erase(it);
                    ++count;
                }
                else
                {
                    ++it;
                }
            }
            return count;
        }

    } // namespace http
} // namespace sylar

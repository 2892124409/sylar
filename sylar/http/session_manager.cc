#include "sylar/http/session_manager.h"

#include "sylar/base/util.h"

#include <sstream>

namespace sylar
{
    namespace http
    {

        SessionManager::SessionManager(uint64_t max_inactive_ms)
            : m_maxInactiveMs(max_inactive_ms), m_nextId(1)
        {
        }

        std::string SessionManager::generateSessionId()
        {
            std::ostringstream ss;
            ss << GetCurrentMS() << "-" << m_nextId++;
            return ss.str();
        }

        Session::ptr SessionManager::create()
        {
            Mutex::Lock lock(m_mutex);
            std::string id = generateSessionId();
            Session::ptr session(new Session(id, GetCurrentMS(), m_maxInactiveMs));
            m_sessions[id] = session;
            return session;
        }

        Session::ptr SessionManager::get(const std::string &id)
        {
            Mutex::Lock lock(m_mutex);
            std::map<std::string, Session::ptr>::iterator it = m_sessions.find(id);
            if (it == m_sessions.end())
            {
                return Session::ptr();
            }
            if (it->second->isExpired(GetCurrentMS()))
            {
                m_sessions.erase(it);
                return Session::ptr();
            }
            it->second->touch(GetCurrentMS());
            return it->second;
        }

        Session::ptr SessionManager::getOrCreate(HttpRequest::ptr request, HttpResponse::ptr response)
        {
            Session::ptr session = get(request->getCookie("SID"));
            if (session)
            {
                return session;
            }
            session = create();
            response->addSetCookie("SID=" + session->getId() + "; Path=/; HttpOnly");
            return session;
        }

        bool SessionManager::remove(const std::string &id)
        {
            Mutex::Lock lock(m_mutex);
            return m_sessions.erase(id) > 0;
        }

        size_t SessionManager::sweepExpired()
        {
            Mutex::Lock lock(m_mutex);
            size_t count = 0;
            uint64_t now = GetCurrentMS();
            for (std::map<std::string, Session::ptr>::iterator it = m_sessions.begin(); it != m_sessions.end();)
            {
                if (it->second->isExpired(now))
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

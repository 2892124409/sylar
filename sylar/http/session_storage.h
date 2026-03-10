#ifndef __SYLAR_HTTP_SESSION_STORAGE_H__
#define __SYLAR_HTTP_SESSION_STORAGE_H__

#include "sylar/http/session.h"
#include "sylar/concurrency/mutex/mutex.h"

#include <map>
#include <memory>
#include <string>

namespace sylar
{
    namespace http
    {

        class SessionStorage
        {
        public:
            typedef std::shared_ptr<SessionStorage> ptr;
            virtual ~SessionStorage() {}

            virtual void save(Session::ptr session) = 0;
            virtual Session::ptr load(const std::string &session_id) = 0;
            virtual bool remove(const std::string &session_id) = 0;
            virtual size_t sweepExpired(uint64_t now_ms) = 0;
        };

        class MemorySessionStorage : public SessionStorage
        {
        public:
            typedef std::shared_ptr<MemorySessionStorage> ptr;

            virtual void save(Session::ptr session) override;
            virtual Session::ptr load(const std::string &session_id) override;
            virtual bool remove(const std::string &session_id) override;
            virtual size_t sweepExpired(uint64_t now_ms) override;

        private:
            Mutex m_mutex;
            std::map<std::string, Session::ptr> m_sessions;
        };

    } // namespace http
} // namespace sylar

#endif

#include "http/session/session.h"

namespace sylar
{
    namespace http
    {

        Session::Session(const std::string &id, uint64_t create_ms, uint64_t max_inactive_ms)
            : m_id(id), m_createTimeMs(create_ms), m_lastAccessTimeMs(create_ms), m_maxInactiveMs(max_inactive_ms)
        {
        }

        void Session::set(const std::string &key, const std::string &value)
        {
            m_data[key] = value;
        }

        std::string Session::get(const std::string &key, const std::string &def) const
        {
            std::map<std::string, std::string>::const_iterator it = m_data.find(key);
            return it == m_data.end() ? def : it->second;
        }

        bool Session::has(const std::string &key) const
        {
            return m_data.find(key) != m_data.end();
        }

        void Session::remove(const std::string &key)
        {
            m_data.erase(key);
        }

        void Session::touch(uint64_t now_ms)
        {
            m_lastAccessTimeMs = now_ms;
        }

        bool Session::isExpired(uint64_t now_ms) const
        {
            return now_ms > m_lastAccessTimeMs && (now_ms - m_lastAccessTimeMs) > m_maxInactiveMs;
        }

    } // namespace http
} // namespace sylar

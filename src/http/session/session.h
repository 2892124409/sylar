#ifndef __SYLAR_HTTP_SESSION_H__
#define __SYLAR_HTTP_SESSION_H__

#include <map>
#include <memory>
#include <string>
#include <stdint.h>

namespace http
{

    /**
     * @brief 单个 HTTP 会话对象
     * @details
     * 这是服务端 Session 的内存载体。
     *
     * 浏览器侧只保存一个 `SID` Cookie；
     * 真正的会话数据保存在服务端的 `Session` 对象里。
     *
     * 当前阶段先把数据结构做成：
     * `string -> string`
     * 这样便于先把会话机制跑通，后续再扩展更复杂的数据模型。
     */
    class Session
    {
    public:
        typedef std::shared_ptr<Session> ptr;

        /**
         * @param id Session 唯一标识
         * @param create_ms 创建时间
         * @param max_inactive_ms 最大非活跃时长
         */
        Session(const std::string &id, uint64_t create_ms, uint64_t max_inactive_ms);

        /// Session ID，一般会写入 Cookie SID
        const std::string &getId() const { return m_id; }
        /// 创建时间
        uint64_t getCreateTime() const { return m_createTimeMs; }
        /// 最后访问时间，用于过期判断
        uint64_t getLastAccessTime() const { return m_lastAccessTimeMs; }
        /// 最大非活跃时长
        uint64_t getMaxInactiveMs() const { return m_maxInactiveMs; }

        /// 设置一个会话键值
        void set(const std::string &key, const std::string &value);

        /// 获取一个会话键值
        std::string get(const std::string &key, const std::string &def = "") const;

        /// 判断会话键是否存在
        bool has(const std::string &key) const;

        /// 删除一个会话键
        void remove(const std::string &key);

        /// 刷新最后访问时间
        void touch(uint64_t now_ms);

        /// 判断当前会话是否已过期
        bool isExpired(uint64_t now_ms) const;

    private:
        /// Session 唯一标识（SID），用于客户端与服务端会话映射，SessionManger通过这个查找对应的Session
        std::string m_id;
        /// Session 创建时间（毫秒时间戳）
        uint64_t m_createTimeMs;
        /// 最近一次访问时间（毫秒时间戳），用于非活跃超时判断
        uint64_t m_lastAccessTimeMs;
        /// 最大非活跃时长（毫秒），超过则视为过期
        uint64_t m_maxInactiveMs;
        /// 会话键值数据（当前阶段为 string -> string）
        std::map<std::string, std::string> m_data;
    };

} // namespace http

#endif

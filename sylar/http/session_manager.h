#ifndef __SYLAR_HTTP_SESSION_MANAGER_H__
#define __SYLAR_HTTP_SESSION_MANAGER_H__

#include "sylar/http/http_request.h"
#include "sylar/http/http_response.h"
#include "sylar/http/session.h"
#include "sylar/concurrency/mutex/mutex.h"

#include <memory>
#include <map>
#include <string>
#include <stdint.h>

namespace sylar {
namespace http {

/**
 * @brief Session 管理器
 * @details
 * 它负责管理服务端所有 Session：
 * - 创建 Session
 * - 按 SID 查找 Session
 * - 自动创建新 Session
 * - 清理过期 Session
 *
 * 你可以把它理解成一个“内存版 Session 仓库”。
 * 当前阶段还没有接数据库或 Redis，先用内存实现最小闭环。
 */
class SessionManager {
public:
    typedef std::shared_ptr<SessionManager> ptr;

    /**
     * @param max_inactive_ms Session 最大非活跃时间，默认 30 分钟
     */
    SessionManager(uint64_t max_inactive_ms = 30 * 60 * 1000);

    /// 创建一个全新的 Session
    Session::ptr create();

    /// 根据 SID 获取 Session，不存在或过期则返回空
    Session::ptr get(const std::string& id);

    /**
     * @brief 获取或创建 Session
     * @details
     * 这是 HTTP 框架里最常用的方法：
     * - 先从请求的 Cookie 中取 `SID`
     * - 如果存在并有效，返回旧 Session
     * - 否则创建新 Session，并通过响应头 `Set-Cookie` 发回客户端
     */
    Session::ptr getOrCreate(HttpRequest::ptr request, HttpResponse::ptr response);

    /// 删除指定 SID 的 Session
    bool remove(const std::string& id);

    /// 清理所有已过期 Session，并返回清理数量
    size_t sweepExpired();

private:
    /// 生成唯一 SID，当前阶段采用时间戳 + 自增序号
    std::string generateSessionId();

private:
    Mutex m_mutex;
    uint64_t m_maxInactiveMs;
    uint64_t m_nextId;
    std::map<std::string, Session::ptr> m_sessions;
};

} // namespace http
} // namespace sylar

#endif

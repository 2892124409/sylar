#ifndef __SYLAR_HTTP_SESSION_MANAGER_H__
#define __SYLAR_HTTP_SESSION_MANAGER_H__

#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/session/session.h"
#include "http/session/session_storage.h"
#include "sylar/fiber/timer.h"
#include "sylar/concurrency/mutex/mutex.h"

#include <memory>
#include <map>
#include <string>
#include <stdint.h>

namespace http
{

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
    class SessionManager : public std::enable_shared_from_this<SessionManager>
    {
    public:
        typedef std::shared_ptr<SessionManager> ptr;

        /**
         * @param max_inactive_ms Session 最大非活跃时间，默认 30 分钟
         */
        SessionManager(uint64_t max_inactive_ms = 30 * 60 * 1000,
                       SessionStorage::ptr storage = SessionStorage::ptr(new MemorySessionStorage()));

        /// 创建一个全新的 Session
        Session::ptr create();

        /// 根据 SID 获取 Session，不存在或过期则返回空
        Session::ptr get(const std::string &id);

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
        bool remove(const std::string &id);

        /// 清理所有已过期 Session，并返回清理数量
        size_t sweepExpired();

        /**
         * @brief 启动周期性过期清理定时器
         * @param manager 定时器管理器（通常传 IOManager）
         * @param sweep_interval_ms 清理周期，默认 60 秒
         * @return 是否成功启动；manager 为空或已启动时返回 false
         */
        bool startSweepTimer(sylar::TimerManager *manager, uint64_t sweep_interval_ms = 60 * 1000);

        /// 停止周期性过期清理定时器
        bool stopSweepTimer();

        /// 当前是否已启动周期性过期清理定时器
        bool hasSweepTimer() const;

    private:
        /// 生成唯一 SID，当前阶段采用时间戳 + 自增序号
        std::string generateSessionId();

    private:
        /// 并发保护锁：保护会话表和自增 ID 的线程安全
        Mutex m_mutex;
        /// 会话最大非活跃时长（毫秒），新建 Session 会继承该配置
        uint64_t m_maxInactiveMs;
        /// 会话 ID 自增序号（与时间戳组合生成 SID）
        uint64_t m_nextId;
        /// 会话存储后端（当前默认是内存存储，可平滑替换为 Redis/DB）
        SessionStorage::ptr m_storage;
        /// 周期性过期清理定时器句柄
        sylar::Timer::ptr m_sweepTimer;
        /// 周期性清理间隔（毫秒）
        uint64_t m_sweepIntervalMs;
    };

} // namespace http

#endif

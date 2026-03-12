#include "http/session/session_manager.h"

#include "http/core/http_framework_config.h"
#include "sylar/base/util.h"

#include <sstream>

namespace http
{

    SessionManager::SessionManager(SessionStorage::ptr storage)
        : SessionManager(HttpFrameworkConfig::GetSessionInactivityTimeoutMs(), storage)
    {
    }

    SessionManager::SessionManager(uint64_t max_inactive_ms, SessionStorage::ptr storage)
        // 会话最大非活跃时长（毫秒）
        : m_maxInactiveMs(max_inactive_ms == 0 ? HttpFrameworkConfig::GetSessionInactivityTimeoutMs() : max_inactive_ms),
          m_nextId(1),
          m_storage(storage ? storage : SessionStorage::ptr(new MemorySessionStorage())),
          m_sweepIntervalMs(0)
    {
        // m_nextId 从 1 开始，避免出现 "...-0" 这种不直观的 SID
        // m_sweepIntervalMs 初始为 0，表示还未启动周期清理
    }

    std::string SessionManager::generateSessionId()
    {
        // 用字符串流拼接 SID，格式：<当前毫秒时间>-<自增序号>
        std::ostringstream ss;
        // GetCurrentMS() 提供当前毫秒时间戳，用于粗粒度时间维度唯一性
        // m_nextId++ 提供同毫秒内的序号区分
        ss << sylar::GetCurrentMS() << "-" << m_nextId++;
        // 返回本次生成的 SID
        return ss.str();
    }

    Session::ptr SessionManager::create()
    {
        // create() 会改动 m_nextId 和 m_sessions，必须加锁保护并发
        sylar::Mutex::Lock lock(m_mutex);
        // 生成新的会话 ID
        std::string id = generateSessionId();
        // 创建 Session：
        // - id: 唯一 SID
        // - create_ms: 当前时间，同时也作为首次访问时间
        // - m_maxInactiveMs: 继承管理器的非活跃超时配置
        Session::ptr session(new Session(id, sylar::GetCurrentMS(), m_maxInactiveMs));
        // 通过存储后端保存新会话
        m_storage->save(session);
        // 返回新建会话
        return session;
    }

    Session::ptr SessionManager::get(const std::string &id)
    {
        // Session ID 为空直接返回空
        if (id.empty())
        {
            return Session::ptr();
        }
        // 从存储后端加载 Session
        Session::ptr session = m_storage->load(id);
        if (!session)
        {
            return Session::ptr();
        }
        // 如果命中但已过期：
        // - 立即从存储删除
        // - 返回空，表示该 SID 已失效
        if (session->isExpired(sylar::GetCurrentMS()))
        {
            m_storage->remove(id);
            return Session::ptr();
        }
        // 命中且有效：刷新最后访问时间（滑动过期）
        session->touch(sylar::GetCurrentMS());
        m_storage->save(session);
        // 返回有效会话对象
        return session;
    }

    Session::ptr SessionManager::getOrCreate(HttpRequest::ptr request, HttpResponse::ptr response)
    {
        // 先尝试从请求 Cookie 读取 SID 并查找会话
        Session::ptr session = get(request->getCookie("SID"));
        if (session)
        {
            // 命中有效旧会话，直接返回
            return session;
        }
        // 没命中（或已过期），创建新会话
        session = create();
        // 通过 Set-Cookie 把新 SID 下发给客户端：
        // - Path=/      表示全站路径可用
        // - HttpOnly    防止前端 JS 直接读取，降低 XSS 风险
        response->addSetCookie("SID=" + session->getId() + "; Path=/; HttpOnly");
        // 返回新会话
        return session;
    }

    bool SessionManager::remove(const std::string &id)
    {
        // 转交给存储后端删除
        return m_storage->remove(id);
    }

    size_t SessionManager::sweepExpired()
    {
        // 存储后端按当前时间批量清理过期会话
        return m_storage->sweepExpired(sylar::GetCurrentMS());
    }

    bool SessionManager::startSweepTimer(sylar::TimerManager *manager, uint64_t sweep_interval_ms)
    {
        // 参数保护：
        // - manager 为空无法注册定时器
        // - 间隔为 0 没有意义
        if (!manager || sweep_interval_ms == 0)
        {
            return false;
        }

        // 保护 m_sweepTimer / m_sweepIntervalMs 的并发访问
        sylar::Mutex::Lock lock(m_mutex);
        // 已经启动过则不重复启动
        if (m_sweepTimer)
        {
            return false;
        }

        // 记录清理周期
        m_sweepIntervalMs = sweep_interval_ms;
        // 获取自身弱引用：
        // - 避免定时器回调把 SessionManager 强行延长生命周期
        // - 对象销毁后，回调可自动失效
        std::weak_ptr<SessionManager> weak_self = shared_from_this();
        // 注册“条件循环定时器”：
        // - 间隔：sweep_interval_ms
        // - 回调：执行 sweepExpired()
        // - 条件：weak_self 有效才触发
        // - recurring=true：周期执行
        m_sweepTimer = manager->addConditionTimer(
            sweep_interval_ms,
            [weak_self]()
            {
                // 回调触发时先尝试提升弱引用
                SessionManager::ptr self = weak_self.lock();
                if (!self)
                {
                    // 管理器对象已销毁，安全退出
                    return;
                }
                // 执行一次批量过期清理
                self->sweepExpired();
            },
            weak_self,
            true);
        // 返回是否注册成功
        return m_sweepTimer != nullptr;
    }

    bool SessionManager::stopSweepTimer()
    {
        // 保护 m_sweepTimer 并发访问
        sylar::Mutex::Lock lock(m_mutex);
        // 尚未启动则返回 false
        if (!m_sweepTimer)
        {
            return false;
        }
        // 取消底层定时器
        bool rt = m_sweepTimer->cancel();
        // 释放句柄，标记为未启动状态
        m_sweepTimer.reset();
        // 返回取消结果
        return rt;
    }

    bool SessionManager::hasSweepTimer() const
    {
        // const 方法里也要加锁读共享状态。
        // 这里用 const_cast 复用同一把互斥锁对象。
        SessionManager *self = const_cast<SessionManager *>(this);
        sylar::Mutex::Lock lock(self->m_mutex);
        // 有句柄即视为已启动
        return m_sweepTimer != nullptr;
    }

} // namespace http

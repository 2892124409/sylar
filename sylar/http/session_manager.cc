#include "sylar/http/session_manager.h"

#include "sylar/base/util.h"

#include <sstream>

namespace sylar
{
    namespace http
    {

        SessionManager::SessionManager(uint64_t max_inactive_ms)
            // 会话最大非活跃时长（毫秒）
            : m_maxInactiveMs(max_inactive_ms), m_nextId(1), m_sweepIntervalMs(0)
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
            ss << GetCurrentMS() << "-" << m_nextId++;
            // 返回本次生成的 SID
            return ss.str();
        }

        Session::ptr SessionManager::create()
        {
            // create() 会改动 m_nextId 和 m_sessions，必须加锁保护并发
            Mutex::Lock lock(m_mutex);
            // 生成新的会话 ID
            std::string id = generateSessionId();
            // 创建 Session：
            // - id: 唯一 SID
            // - create_ms: 当前时间，同时也作为首次访问时间
            // - m_maxInactiveMs: 继承管理器的非活跃超时配置
            Session::ptr session(new Session(id, GetCurrentMS(), m_maxInactiveMs));
            // 放入内存会话表
            m_sessions[id] = session;
            // 返回新建会话
            return session;
        }

        Session::ptr SessionManager::get(const std::string &id)
        {
            // get() 需要读写 m_sessions（命中过期会删除），因此也要加锁
            Mutex::Lock lock(m_mutex);
            // 在会话表中查找 SID
            std::map<std::string, Session::ptr>::iterator it = m_sessions.find(id);
            if (it == m_sessions.end())
            {
                // 不存在则返回空指针
                return Session::ptr();
            }
            // 如果命中但已过期：
            // - 立即从会话表删除
            // - 返回空，表示该 SID 已失效
            if (it->second->isExpired(GetCurrentMS()))
            {
                m_sessions.erase(it);
                return Session::ptr();
            }
            // 命中且有效：刷新最后访问时间（滑动过期）
            it->second->touch(GetCurrentMS());
            // 返回有效会话对象
            return it->second;
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
            // 删除会话会写 m_sessions，需加锁
            Mutex::Lock lock(m_mutex);
            // erase 返回删除数量，>0 表示删除成功
            return m_sessions.erase(id) > 0;
        }

        size_t SessionManager::sweepExpired()
        {
            // 批量清理会改动 m_sessions，需加锁
            Mutex::Lock lock(m_mutex);
            // 统计本轮清理数量
            size_t count = 0;
            // 统一取一次当前时间，避免循环内多次系统调用
            uint64_t now = GetCurrentMS();
            // 遍历会话表并删除过期项
            for (std::map<std::string, Session::ptr>::iterator it = m_sessions.begin(); it != m_sessions.end();)
            {
                if (it->second->isExpired(now))
                {
                    // erase(it) 返回下一个有效迭代器，适合边遍历边删除
                    it = m_sessions.erase(it);
                    // 清理计数 +1
                    ++count;
                }
                else
                {
                    // 未过期则继续下一个
                    ++it;
                }
            }
            // 返回本轮清理总数
            return count;
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
            Mutex::Lock lock(m_mutex);
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
            Mutex::Lock lock(m_mutex);
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
            Mutex::Lock lock(self->m_mutex);
            // 有句柄即视为已启动
            return m_sweepTimer != nullptr;
        }

    } // namespace http
} // namespace sylar

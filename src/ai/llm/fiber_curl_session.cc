#include "ai/llm/fiber_curl_session.h"

#include "log/logger.h"

#include <vector>

namespace ai
{
namespace llm
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

FiberCurlSession::FiberCurlSession(CURL* easy)
    : m_easy(easy)
    , m_multi(nullptr)
    , m_iom(sylar::IOManager::GetThis())
    , m_wait_thread(-1)
    , m_waiting(false)
    , m_resume_scheduled(false)
    , m_done(false)
    , m_result(CURLE_OK)
{
    if (m_iom)
    {
        m_wait_fiber = sylar::Fiber::GetThis();
        if (m_wait_fiber)
        {
            m_wait_thread = m_wait_fiber->getBoundThread();
        }
    }
}

FiberCurlSession::~FiberCurlSession()
{
    Cleanup();
}

int FiberCurlSession::SocketCallback(CURL*, curl_socket_t s, int what, void* userp, void*)
{
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    if (!self)
    {
        return 0;
    }

    self->RegisterSocketWatch(s, what);
    return 0;
}

int FiberCurlSession::TimerCallback(CURLM*, long timeout_ms, void* userp)
{
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    if (!self)
    {
        return 0;
    }

    self->UpdateTimer(timeout_ms);
    return 0;
}

void FiberCurlSession::OnSocketEvent(curl_socket_t fd, int action)
{
    bool need_schedule = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending_actions.push_back(std::make_pair(fd, action));
        if (m_waiting && !m_resume_scheduled)
        {
            m_resume_scheduled = true;
            need_schedule = true;
        }
    }

    if (need_schedule && m_iom && m_wait_fiber)
    {
        m_iom->schedule(m_wait_fiber, m_wait_thread);
    }
}

void FiberCurlSession::RegisterSocketWatch(curl_socket_t fd, int what)
{
    if (!m_iom)
    {
        return;
    }

    if (what == CURL_POLL_REMOVE)
    {
        CancelSocketWatch(fd);
        return;
    }

    const int target_events = ((what == CURL_POLL_IN || what == CURL_POLL_INOUT) ? sylar::IOManager::READ : 0) |
                              ((what == CURL_POLL_OUT || what == CURL_POLL_INOUT) ? sylar::IOManager::WRITE : 0);

    CancelSocketWatch(fd);

    if (target_events & sylar::IOManager::READ)
    {
        int rt = m_iom->addEvent(
            fd,
            sylar::IOManager::READ,
            [this, fd]()
            {
                OnSocketEvent(fd, CURL_CSELECT_IN);
            });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "add READ event failed for fd=" << fd;
        }
    }

    if (target_events & sylar::IOManager::WRITE)
    {
        int rt = m_iom->addEvent(
            fd,
            sylar::IOManager::WRITE,
            [this, fd]()
            {
                OnSocketEvent(fd, CURL_CSELECT_OUT);
            });
        if (rt != 0)
        {
            BASE_LOG_WARN(g_logger) << "add WRITE event failed for fd=" << fd;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_watch_events[fd] = target_events;
}

void FiberCurlSession::CancelSocketWatch(curl_socket_t fd)
{
    if (!m_iom)
    {
        return;
    }

    m_iom->delEvent(fd, sylar::IOManager::READ);
    m_iom->delEvent(fd, sylar::IOManager::WRITE);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_watch_events.erase(fd);
}

void FiberCurlSession::UpdateTimer(long timeout_ms)
{
    if (!m_iom)
    {
        return;
    }

    if (m_timer)
    {
        m_timer->cancel();
        m_timer.reset();
    }

    if (timeout_ms < 0)
    {
        return;
    }

    if (timeout_ms == 0)
    {
        OnSocketEvent(CURL_SOCKET_TIMEOUT, 0);
        return;
    }

    m_timer = m_iom->addTimer(static_cast<uint64_t>(timeout_ms),
                              [this]()
                              {
                                  OnSocketEvent(CURL_SOCKET_TIMEOUT, 0);
                              },
                              false);
}

void FiberCurlSession::WaitForSignal()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pending_actions.empty())
        {
            return;
        }
        m_waiting = true;
        m_resume_scheduled = false;
    }

    sylar::Fiber::YieldToHold();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_waiting = false;
    m_resume_scheduled = false;
}

bool FiberCurlSession::PopPendingAction(curl_socket_t& fd, int& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending_actions.empty())
    {
        return false;
    }

    std::pair<curl_socket_t, int> item = m_pending_actions.front();
    m_pending_actions.pop_front();
    fd = item.first;
    action = item.second;
    return true;
}

void FiberCurlSession::DrainMessages()
{
    if (!m_multi)
    {
        return;
    }

    int remaining = 0;
    while (true)
    {
        CURLMsg* msg = curl_multi_info_read(m_multi, &remaining);
        if (!msg)
        {
            break;
        }

        if (msg->msg == CURLMSG_DONE)
        {
            m_done = true;
            m_result = msg->data.result;
        }
    }
}

void FiberCurlSession::Cleanup()
{
    if (m_timer)
    {
        m_timer->cancel();
        m_timer.reset();
    }

    if (m_iom)
    {
        std::vector<curl_socket_t> fds;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (std::map<curl_socket_t, int>::const_iterator it = m_watch_events.begin();
                 it != m_watch_events.end();
                 ++it)
            {
                fds.push_back(it->first);
            }
            m_watch_events.clear();
            m_pending_actions.clear();
        }

        for (size_t i = 0; i < fds.size(); ++i)
        {
            m_iom->delEvent(fds[i], sylar::IOManager::READ);
            m_iom->delEvent(fds[i], sylar::IOManager::WRITE);
        }
    }

    if (m_multi)
    {
        if (m_easy)
        {
            curl_multi_remove_handle(m_multi, m_easy);
        }
        curl_multi_cleanup(m_multi);
        m_multi = nullptr;
    }
}

CURLcode FiberCurlSession::Perform()
{
    if (!m_easy)
    {
        return CURLE_FAILED_INIT;
    }

    if (!m_iom || !m_wait_fiber)
    {
        return curl_easy_perform(m_easy);
    }

    m_multi = curl_multi_init();
    if (!m_multi)
    {
        return CURLE_FAILED_INIT;
    }

    curl_multi_setopt(m_multi, CURLMOPT_SOCKETFUNCTION, &FiberCurlSession::SocketCallback);
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERFUNCTION, &FiberCurlSession::TimerCallback);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERDATA, this);

    CURLMcode mcode = curl_multi_add_handle(m_multi, m_easy);
    if (mcode != CURLM_OK)
    {
        Cleanup();
        return CURLE_FAILED_INIT;
    }

    int running = 0;
    mcode = curl_multi_socket_action(m_multi, CURL_SOCKET_TIMEOUT, 0, &running);
    if (mcode != CURLM_OK)
    {
        Cleanup();
        return CURLE_RECV_ERROR;
    }

    DrainMessages();

    while (!m_done && running > 0)
    {
        WaitForSignal();

        curl_socket_t fd = 0;
        int action = 0;
        while (PopPendingAction(fd, action))
        {
            mcode = curl_multi_socket_action(m_multi, fd, action, &running);
            if (mcode != CURLM_OK)
            {
                Cleanup();
                return CURLE_RECV_ERROR;
            }
            DrainMessages();
        }

        if (!m_done && running > 0)
        {
            DrainMessages();
        }
    }

    if (!m_done)
    {
        DrainMessages();
    }

    CURLcode result = m_result;
    Cleanup();
    return result;
}

} // namespace llm
} // namespace ai

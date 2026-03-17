#ifndef __SYLAR_AI_LLM_FIBER_CURL_SESSION_H__
#define __SYLAR_AI_LLM_FIBER_CURL_SESSION_H__

#include "sylar/fiber/fiber.h"
#include "sylar/fiber/iomanager.h"

#include <curl/curl.h>

#include <deque>
#include <map>
#include <mutex>

namespace ai
{
namespace llm
{

/**
 * @brief 把一次 libcurl 请求纳入 sylar Fiber 调度。
 */
class FiberCurlSession
{
  public:
    explicit FiberCurlSession(CURL* easy);
    ~FiberCurlSession();

    /**
     * @brief 执行请求。
     * @details 当前线程无 IOManager 时自动退化为 curl_easy_perform。
     */
    CURLcode Perform();

  private:
    static int SocketCallback(CURL* easy,
                              curl_socket_t s,
                              int what,
                              void* userp,
                              void* socketp);

    static int TimerCallback(CURLM* multi,
                             long timeout_ms,
                             void* userp);

    void OnSocketEvent(curl_socket_t fd, int action);
    void RegisterSocketWatch(curl_socket_t fd, int what);
    void CancelSocketWatch(curl_socket_t fd);
    void UpdateTimer(long timeout_ms);

    void WaitForSignal();
    bool PopPendingAction(curl_socket_t& fd, int& action);
    void DrainMessages();
    void Cleanup();

  private:
    CURL* m_easy;
    CURLM* m_multi;

    sylar::IOManager* m_iom;
    sylar::Fiber::ptr m_wait_fiber;
    int m_wait_thread;

    std::mutex m_mutex;
    std::deque<std::pair<curl_socket_t, int> > m_pending_actions;
    std::map<curl_socket_t, int> m_watch_events;

    sylar::Timer::ptr m_timer;
    bool m_waiting;
    bool m_resume_scheduled;
    bool m_done;
    CURLcode m_result;
};

} // namespace llm
} // namespace ai

#endif

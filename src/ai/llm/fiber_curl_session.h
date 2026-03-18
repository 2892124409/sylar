#ifndef __SYLAR_AI_LLM_FIBER_CURL_SESSION_H__
#define __SYLAR_AI_LLM_FIBER_CURL_SESSION_H__

#include "sylar/fiber/fiber.h"
#include "sylar/fiber/iomanager.h"

#include <curl/curl.h>

#include <deque>
#include <map>
#include <mutex>

/**
 * @file fiber_curl_session.h
 * @brief libcurl 请求的 fiber 协程化执行封装。
 */

namespace ai
{
namespace llm
{

/**
 * @brief 把一次 libcurl 请求纳入 sylar Fiber 调度。
 * @details
 * 在 IOManager 存在时，通过 `curl_multi + socket/timer` 回调驱动，
 * 让当前 fiber 在等待网络事件期间 yield，不阻塞工作线程。
 * 若当前线程无 IOManager，则自动退化为 `curl_easy_perform`。
 */
class FiberCurlSession
{
  public:
    /**
     * @brief 构造请求会话。
     * @param easy 已初始化的 CURL easy handle（不拥有其生命周期）。
     */
    explicit FiberCurlSession(CURL* easy);
    /** @brief 析构时自动清理 multi/timer/fd 监听资源。 */
    ~FiberCurlSession();

    /**
     * @brief 执行请求。
     * @details 当前线程无 IOManager 时自动退化为 curl_easy_perform。
     * @return libcurl 错误码（`CURLE_OK` 表示成功）。
     */
    CURLcode Perform();

  private:
    /**
     * @brief libcurl socket 回调：通知上层注册/更新 fd 监听。
     * @param easy 当前事件所属的 easy handle（为匹配 libcurl 回调签名保留，本实现不直接使用）。
     * @param s 触发回调的 socket fd。
     * @param what libcurl 事件类型（如 `CURL_POLL_IN`/`CURL_POLL_OUT`/`CURL_POLL_REMOVE`）。
     * @param userp 通过 `CURLMOPT_SOCKETDATA` 传入的用户指针，实际为 `FiberCurlSession*`。
     * @param socketp 通过 `curl_multi_assign` 绑定的 socket 私有指针（本实现未使用）。
     * @return 0 表示回调处理完成（沿用 libcurl 约定）。
     */
    static int SocketCallback(CURL* easy,
                              curl_socket_t s,
                              int what,
                              void* userp,
                              void* socketp);

    /**
     * @brief libcurl timer 回调：通知上层注册/更新超时定时器。
     * @param multi 当前 multi handle（为匹配 libcurl 回调签名保留，本实现不直接使用）。
     * @param timeout_ms libcurl 给出的下次超时时间（毫秒）：
     * `-1` 表示取消定时器，`0` 表示立即触发，`>0` 表示延时触发。
     * @param userp 通过 `CURLMOPT_TIMERDATA` 传入的用户指针，实际为 `FiberCurlSession*`。
     * @return 0 表示回调处理完成（沿用 libcurl 约定）。
     */
    static int TimerCallback(CURLM* multi,
                             long timeout_ms,
                             void* userp);

    /**
     * @brief 记录 socket 事件并在需要时恢复等待中的 fiber。
     * @param fd 事件对应的 socket fd；定时器触发时可能是 `CURL_SOCKET_TIMEOUT`。
     * @param action socket 动作掩码（如 `CURL_CSELECT_IN`/`CURL_CSELECT_OUT`）。
     */
    void OnSocketEvent(curl_socket_t fd, int action);
    /**
     * @brief 根据 libcurl 事件类型注册 READ/WRITE 监听。
     * @param fd 需要监听的 socket fd。
     * @param what libcurl 关注的事件类型（`CURL_POLL_*`）。
     */
    void RegisterSocketWatch(curl_socket_t fd, int what);
    /** @brief 在 sylar 一次性事件模型下重挂载 fd 监听。 */
    void RearmSocketWatch(curl_socket_t fd);
    /**
     * @brief 取消指定 fd 的 READ/WRITE 监听。
     * @param fd 要取消监听的 socket fd。
     */
    void CancelSocketWatch(curl_socket_t fd);
    /**
     * @brief 更新 curl 驱动定时器。
     * @param timeout_ms 下一次超时（毫秒）；语义同 `TimerCallback`。
     */
    void UpdateTimer(long timeout_ms);

    /** @brief 当无待处理事件时挂起当前 fiber，等待 IO/Timer 唤醒。 */
    void WaitForSignal();
    /**
     * @brief 弹出一个待处理 socket action。
     * @param[out] fd 出参，返回事件对应的 socket fd。
     * @param[out] action 出参，返回事件动作掩码。
     * @return `true` 表示成功弹出一条事件；`false` 表示队列为空。
     */
    bool PopPendingAction(curl_socket_t& fd, int& action);
    /** @brief 消费 multi 消息队列，提取请求完成状态。 */
    void DrainMessages();
    /** @brief 统一清理资源：timer、fd 监听、multi handle。 */
    void Cleanup();

  private:
    /** @brief 外部传入的 easy handle。 */
    CURL* m_easy;
    /** @brief 本次请求私有的 multi handle。 */
    CURLM* m_multi;

    /** @brief 当前线程 IOManager（可能为空）。 */
    sylar::IOManager* m_iom;
    /** @brief 执行 Perform 的当前 fiber。 */
    sylar::Fiber::ptr m_wait_fiber;
    /** @brief 该 fiber 绑定线程 id。 */
    int m_wait_thread;

    /** @brief 保护 pending_actions/watch_events 等共享状态。 */
    std::mutex m_mutex;
    /** @brief 待处理 socket action 队列。 */
    std::deque<std::pair<curl_socket_t, int>> m_pending_actions;
    /** @brief 已注册 fd 及其监听事件掩码。 */
    std::map<curl_socket_t, int> m_watch_events;
    /** @brief 当前已在 IOManager 挂载的事件掩码（用于避免重复 addEvent）。 */
    std::map<curl_socket_t, int> m_armed_events;

    /** @brief curl 定时器在 IOManager 上的映射对象。 */
    sylar::Timer::ptr m_timer;
    /** @brief 当前 fiber 是否处于等待状态。 */
    bool m_waiting;
    /** @brief 是否已经安排过一次恢复，防止重复 schedule。 */
    bool m_resume_scheduled;
    /** @brief 请求是否已完成。 */
    bool m_done;
    /** @brief 请求最终结果。 */
    CURLcode m_result;
};

} // namespace llm
} // namespace ai

#endif

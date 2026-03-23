#ifndef __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__
#define __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

namespace http
{

class HttpFrameworkConfig
{
  public:
    enum ErrorResponseFormat
    {
        ERROR_FORMAT_TEXT = 0,
        ERROR_FORMAT_JSON,
    };

    /**
     * @brief 获取 HTTP 请求头最大大小（字节）
     * @details
     * Parser 会按此值限制请求头缓冲增长。
     * 重载形式：运行时动态读取型（后续请求立即生效）。
     */
    static size_t GetMaxHeaderSize();

    /**
     * @brief 设置 HTTP 请求头最大大小（字节）
     * @details
     * 后续请求解析流程会按新值校验。
     */
    static void SetMaxHeaderSize(size_t value);

    /**
     * @brief 获取 HTTP 请求体最大大小（字节）
     * @details
     * Parser 会按 Content-Length 与该值比较并提前拒绝过大请求。
     * 重载形式：运行时动态读取型（后续请求立即生效）。
     */
    static size_t GetMaxBodySize();

    /**
     * @brief 设置 HTTP 请求体最大大小（字节）
     * @details
     * 后续请求解析流程会按新值校验。
     */
    static void SetMaxBodySize(size_t value);

    /**
     * @brief 获取 HTTP 连接读超时（毫秒）
     * @details
     * 当前在 HttpServer 构造时写入 TcpServer::recvTimeout。
     * 重载形式：启动参数型（已启动服务不自动重配）。
     */
    static uint64_t GetConnectionTimeoutMs();

    /**
     * @brief 设置 HTTP 连接读超时（毫秒）
     * @details
     * 建议在服务启动前设置；0 会回退到默认值。
     */
    static void SetConnectionTimeoutMs(uint64_t value);

    /**
     * @brief 获取 HTTP 自动 Session 开关
     * @details
     * 为 false 时，HttpServer 不再自动读取/创建 Session，也不会写回 SID Cookie。
     * 重载形式：运行时动态读取型（后续请求立即生效）。
     */
    static bool GetSessionEnabled();

    /**
     * @brief 设置 HTTP 自动 Session 开关
     * @details
     * 设置后后续请求是否自动附带 Session 行为立即切换。
     */
    static void SetSessionEnabled(bool value);

    /**
     * @brief 获取 Session 最大非活跃时间（毫秒）
     * @details
     * 当前在新建 SessionManager 或显式传 0 时生效。
     * 重载形式：新对象生效型（已存在 SessionManager 不自动重配）。
     */
    static uint64_t GetSessionInactivityTimeoutMs();

    /**
     * @brief 设置 Session 最大非活跃时间（毫秒）
     * @details
     * 仅影响后续新建的 SessionManager；0 会回退到默认值。
     */
    static void SetSessionInactivityTimeoutMs(uint64_t value);

    /**
     * @brief 获取 Session 后台清理周期（毫秒）
     * @details
     * 当前在 HttpServer 启动时用于创建清理定时器。
     * 重载形式：启动参数型（运行中修改需重建定时器才完全生效）。
     */
    static uint64_t GetSessionSweepIntervalMs();

    /**
     * @brief 设置 Session 后台清理周期（毫秒）
     * @details
     * 建议在服务启动前设置；运行中设置需配合 stop/startSweepTimer。
     */
    static void SetSessionSweepIntervalMs(uint64_t value);

    /**
     * @brief 获取 SSE 心跳建议间隔（毫秒）
     * @details
     * 该值供 SSE 业务层发送心跳策略参考。
     * 重载形式：新对象生效型（已运行中的流式发送逻辑不会自动改节奏）。
     */
    static uint64_t GetSSEHeartbeatIntervalMs();

    /**
     * @brief 设置 SSE 心跳建议间隔（毫秒）
     * @details
     * 影响后续新启动的 SSE 发送逻辑。
     */
    static void SetSSEHeartbeatIntervalMs(uint64_t value);

    /**
     * @brief 获取 HTTP socket 单次读取缓冲大小（字节）
     * @details
     * 当前在 HttpContext::recvRequest 中按次读取。
     * 重载形式：运行时动态读取型（后续读取立即生效）。
     */
    static size_t GetSocketReadBufferSize();

    /**
     * @brief 设置 HTTP socket 单次读取缓冲大小（字节）
     * @details
     * 后续读取流程立即按新值申请缓冲；0 会回退到默认值。
     */
    static void SetSocketReadBufferSize(size_t value);

    /**
     * @brief 获取 HTTP 最大并发连接数
     * @details
     * 0 表示不限制；当前通过监听器同步到 HttpServer 的运行时检查缓存。
     * 重载形式：监听器驱动缓存同步型（配置变化后，新连接检查立即生效）。
     */
    static uint32_t GetMaxConnections();

    /**
     * @brief 设置 HTTP 最大并发连接数
     * @details
     * 0 表示不限制；设置后后续新连接立即按新上限校验。
     */
    static void SetMaxConnections(uint32_t value);

    /**
     * @brief 获取 keep-alive 空闲超时（毫秒）
     * @details
     * 当前通过监听器同步到 HttpServer 的运行时缓存，
     * 用于同一连接上第 2 个及之后请求的等待超时。
     * 重载形式：监听器驱动缓存同步型（配置变化后，后续 keep-alive 等待立即生效）。
     */
    static uint64_t GetKeepAliveTimeoutMs();

    /**
     * @brief 设置 keep-alive 空闲超时（毫秒）
     * @details
     * 设置后后续 keep-alive 等待立即按新值执行；0 会回退到默认值。
     */
    static void SetKeepAliveTimeoutMs(uint64_t value);

    /**
     * @brief 获取单连接 keep-alive 最大请求数
     * @details
     * 0 表示不限制；当前通过监听器同步到 HttpServer 的运行时缓存。
     * 重载形式：监听器驱动缓存同步型（配置变化后，后续响应立即按新上限决策）。
     */
    static uint32_t GetKeepAliveMaxRequests();

    /**
     * @brief 设置单连接 keep-alive 最大请求数
     * @details
     * 0 表示不限制；设置后后续响应立即按新上限决定是否断开连接。
     */
    static void SetKeepAliveMaxRequests(uint32_t value);

    /**
     * @brief 获取默认 HTTP IO worker 线程数
     * @details
     * 当前仅在 HttpServer::CreateWithConfig() 中读取，用于创建内部 IOManager。
     * 重载形式：启动参数型（已创建的 worker 不会自动扩缩容）。
     */
    static uint32_t GetIOWorkerThreads();

    /**
     * @brief 设置默认 HTTP IO worker 线程数
     * @details
     * 仅影响后续通过 HttpServer::CreateWithConfig() 创建的服务器；0 会回退到默认值。
     */
    static void SetIOWorkerThreads(uint32_t value);

    /**
     * @brief 获取默认 HTTP accept worker 线程数
     * @details
     * 当前仅在 HttpServer::CreateWithConfig() 中读取，用于创建内部 accept IOManager。
     * 重载形式：启动参数型（已创建的 worker 不会自动扩缩容）。
     */
    static uint32_t GetAcceptWorkerThreads();

    /**
     * @brief 设置默认 HTTP accept worker 线程数
     * @details
     * 仅影响后续通过 HttpServer::CreateWithConfig() 创建的服务器；0 会回退到默认值。
     */
    static void SetAcceptWorkerThreads(uint32_t value);

    /**
     * @brief 获取默认错误响应格式
     * @details
     * 取值为 TEXT 或 JSON。
     * 重载形式：运行时动态读取型（后续新构造的错误响应立即生效）。
     */
    static ErrorResponseFormat GetErrorResponseFormat();

    /**
     * @brief 设置默认错误响应格式
     * @details
     * 设置后，后续错误响应将按新格式输出。
     */
    static void SetErrorResponseFormat(ErrorResponseFormat value);
};

} // namespace http

#endif

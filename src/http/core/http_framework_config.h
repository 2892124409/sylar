#ifndef __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__
#define __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

namespace sylar
{
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
} // namespace sylar

#endif

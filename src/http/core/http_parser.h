#ifndef __SYLAR_HTTP_HTTP_PARSER_H__
#define __SYLAR_HTTP_HTTP_PARSER_H__

#include "http/core/http_request.h"

#include <memory>
#include <string>

namespace http
{

    /**
     * @brief HTTP 请求解析器
     * @details
     * 这个类负责把“接收缓冲区中的原始字节流”解析成 `HttpRequest`。
     *
     * 它当前采用的是最小可用策略：
     * 1. 找到 `\r\n\r\n`，确定 header 结束；
     * 2. 解析请求行与 header；
     * 3. 根据 `Content-Length` 判断 body 是否完整；
     * 4. 如果完整，则返回请求对象并告知消费了多少字节；
     * 5. 如果不完整，则返回空指针，等待更多数据。
     *
     * 这就是当前 HTTP 框架解决 TCP 半包/粘包的核心入口。
     */
    class HttpRequestParser
    {
    public:
        typedef std::shared_ptr<HttpRequestParser> ptr;

        enum ErrorCode
        {
            ERROR_NONE = 0,
            ERROR_INVALID_REQUEST,
            ERROR_REQUEST_TOO_LARGE,
        };

        /// 构造一个干净的解析器
        HttpRequestParser();

        /**
         * @brief 从缓冲区中尝试解析一条请求
         * @param buffer 连接级接收缓冲区
         * @param consumed 成功时返回“消费了多少字节”
         * @return
         * - 成功：返回 `HttpRequest`
         * - 数据不完整：返回空指针，且 `consumed=0`
         * - 解析错误：返回空指针，并设置错误状态
         */
        HttpRequest::ptr parse(const std::string &buffer, size_t &consumed);

        /// 当前解析器是否进入错误状态
        bool hasError() const { return m_error; }

        /// 最近一次错误类型
        ErrorCode getErrorCode() const { return m_errorCode; }

        /// 当前错误是否属于请求过大
        bool isRequestTooLarge() const { return m_errorCode == ERROR_REQUEST_TOO_LARGE; }

        /// 错误原因字符串，便于调试或返回 400
        const std::string &getError() const { return m_errorMessage; }

        /// 设置请求头最大大小限制（字节）
        static void SetMaxHeaderSize(size_t value);
        static size_t GetMaxHeaderSize();

        /// 设置请求体最大大小限制（字节）
        static void SetMaxBodySize(size_t value);
        static size_t GetMaxBodySize();

        /// 清空错误状态，为下一轮解析准备
        void reset();

    private:
        /// 解析器是否处于错误状态（遇到非法请求格式时置为 true）
        bool m_error;

        /// 最近一次错误类型，便于上层区分 400 / 413
        ErrorCode m_errorCode;

        /// 最近一次解析错误的原因描述，便于上层返回 400 或打印日志
        std::string m_errorMessage;
    };

} // namespace http

#endif

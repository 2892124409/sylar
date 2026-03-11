// 头文件保护宏：防止头文件被重复包含。
#ifndef __SYLAR_HTTP_HTTP_CONTEXT_H__
// 定义头文件保护宏。
#define __SYLAR_HTTP_HTTP_CONTEXT_H__

// 引入 HTTP 请求解析器，用于把缓冲区解析为 HttpRequest。
#include "http/core/http_parser.h"
// 引入 SocketStream，用于从连接读取原始字节流。
#include "sylar/net/socket_stream.h"

// sylar 顶层命名空间。
namespace http
{

    /**
     * @brief HTTP 请求解析上下文
     * @details
     * 这个类代表“一个 HTTP 连接上的请求解析上下文”。
     *
     * 它负责把连接级别的解析状态集中起来管理，包括：
     * - 接收缓冲区 m_buffer
     * - 请求解析器 m_parser
     * - 半包/粘包/keep-alive 下多请求复用
     *
     * 这样 HttpSession 就不必再同时管理 socket 与解析细节，
     * 职责边界会更加清晰：
     * - HttpContext：连接级解析上下文
     * - HttpSession：HTTP 连接封装
     * - HttpServer：请求处理主循环
     */
    class HttpContext
    {
    public:
        // HttpContext 智能指针别名。
        typedef std::shared_ptr<HttpContext> ptr;

        /**
         * @brief 从流中接收并解析一条完整 HTTP 请求
         * @param stream 当前连接对应的字节流对象
         * @return
         * - 成功：返回 HttpRequest
         * - 失败/断连/解析错误：返回空指针
         */
        HttpRequest::ptr recvRequest(SocketStream &stream);

        /// 当前解析器是否处于错误状态。
        bool hasError() const { return m_parser.hasError(); }

        /// 最近一次解析错误原因。
        const std::string &getError() const { return m_parser.getError(); }

        /// 最近一次解析错误是否属于请求过大。
        bool isRequestTooLarge() const { return m_parser.isRequestTooLarge(); }

        /// 重置解析器错误状态（通常为调试或显式复位预留）。
        void reset();

    private:
        /// HTTP 请求解析器：负责 buffer -> HttpRequest 的纯解析逻辑。
        HttpRequestParser m_parser;

        /// 连接级接收缓冲区：跨多次 read() 累积数据，处理半包与粘包。
        std::string m_buffer;

        /// 读偏移量：标记 m_buffer 中已消费但尚未 erase 的前缀长度。
        /// 解析成功后只推进偏移量，延迟到下一次 recvRequest 入口才执行
        /// 一次 erase 紧凑。这样在 keep-alive 场景下，上层处理请求期间
        /// 无需承担 O(n) 搬移开销。
        size_t m_offset = 0;
    };

} // namespace http

// 结束头文件保护宏。
#endif

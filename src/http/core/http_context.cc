// 引入 HttpContext 声明。
#include "http/core/http_context.h"

// sylar 顶层命名空间。
namespace sylar
{
    // http 子命名空间。
    namespace http
    {

        // 从流中持续读取数据，直到解析出一条完整请求或出现错误。
        HttpRequest::ptr HttpContext::recvRequest(SocketStream &stream)
        {
            // 循环读取并解析，直到成功返回请求或失败退出。
            while (true)
            {
                // 记录本轮 parse 消费的字节数。
                size_t consumed = 0;
                // 先尝试直接解析当前缓冲区中的数据。
                HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);

                // 解析成功：移除已消费字节，并返回请求对象。
                if (request)
                {
                    // 保留未消费尾部数据，支持粘包与 keep-alive 多请求。
                    m_buffer.erase(0, consumed);
                    // 返回成功解析出的请求。
                    return request;
                }

                // 解析器已进入错误状态：直接返回空指针，让上层决定如何处理。
                if (m_parser.hasError())
                {
                    return HttpRequest::ptr();
                }

                // 当前数据还不完整，继续从底层流读取更多字节。
                char buffer[4096];
                // 从流中读取数据块。
                int rt = stream.read(buffer, sizeof(buffer));

                // 读取失败或连接关闭：返回空指针。
                if (rt <= 0)
                {
                    return HttpRequest::ptr();
                }

                // 把新读到的数据追加到连接级缓冲区，等待下一轮解析。
                m_buffer.append(buffer, rt);
            }
        }

        // 显式重置解析器错误状态。
        void HttpContext::reset()
        {
            // 复位内部解析器的错误信息。
            m_parser.reset();
            // 清空连接级缓冲区，回到初始上下文状态。
            m_buffer.clear();
        }

    } // namespace http
} // namespace sylar

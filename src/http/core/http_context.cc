// 引入 HttpContext 声明。
#include "http/core/http_context.h"

#include "http/core/http_framework_config.h"

#include <vector>

// sylar 顶层命名空间。
namespace http
{

    // 从流中持续读取数据，直到解析出一条完整请求或出现错误。
    HttpRequest::ptr HttpContext::recvRequest(sylar::SocketStream &stream)
    {
        // 循环读取并解析，直到成功返回请求或失败退出。
        while (true)
        {
            // 如果有未消费的前缀，先紧凑一次再解析。
            // 这样 parser 始终拿到干净的 m_buffer 引用，无需临时子串。
            if (m_offset > 0)
            {
                m_buffer.erase(0, m_offset);
                m_offset = 0;
            }

            size_t consumed = 0;
            HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);

            // 解析成功：记录偏移量而不立刻 erase，下次循环再紧凑。
            if (request)
            {
                // 累积偏移量，延迟 erase 到下一轮循环入口。
                // 对于 keep-alive 连接，下一次 recvRequest 调用时才紧凑，
                // 避免每次解析成功都做一次 O(n) 搬移。
                m_offset = consumed;
                return request;
            }

            // 解析器已进入错误状态：直接返回空指针，让上层决定如何处理。
            if (m_parser.hasError())
            {
                return HttpRequest::ptr();
            }

            // 当前数据还不完整，继续从底层流读取更多字节。
            std::vector<char> buffer(HttpFrameworkConfig::GetSocketReadBufferSize());
            int rt = stream.read(buffer.data(), buffer.size());

            // 读取失败或连接关闭：返回空指针。
            if (rt <= 0)
            {
                return HttpRequest::ptr();
            }

            // 把新读到的数据追加到连接级缓冲区，等待下一轮解析。
            m_buffer.append(buffer.data(), rt);
        }
    }

    // 显式重置解析器错误状态。
    void HttpContext::reset()
    {
        // 复位内部解析器的错误信息。
        m_parser.reset();
        // 清空连接级缓冲区和偏移量，回到初始上下文状态。
        m_buffer.clear();
        m_offset = 0;
    }

} // namespace http

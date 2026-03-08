#include "sylar/http/http_session.h"

namespace sylar
{
    namespace http
    {

        HttpSession::HttpSession(Socket::ptr sock, bool owner)
            : SocketStream(sock, owner)
        {
        }

        HttpRequest::ptr HttpSession::recvRequest()
        {
            // 循环读取并解析，直到成功解析出一个完整请求或遇到错误
            while (true)
            {
                // 步骤1：尝试从当前缓冲区解析请求
                // consumed 用于记录本次解析消费了多少字节
                size_t consumed = 0;
                HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);
                
                // 步骤2：解析成功，返回请求对象
                if (request)
                {
                    // 从缓冲区删除已消费的字节
                    // 剩余字节可能是下一个请求的开头（粘包场景）
                    m_buffer.erase(0, consumed);
                    return request;
                }
                
                // 步骤3：解析出错（格式错误），返回空指针
                if (m_parser.hasError())
                {
                    return HttpRequest::ptr();
                }

                // 步骤4：数据不完整（半包），继续从 socket 读取更多数据
                char buffer[4096];
                int rt = read(buffer, sizeof(buffer));
                
                // 读取失败或连接关闭，返回空指针
                if (rt <= 0)
                {
                    return HttpRequest::ptr();
                }
                
                // 将新读取的数据追加到连接级缓冲区
                // 然后回到循环开头重新尝试解析
                m_buffer.append(buffer, rt);
            }
        }

        int HttpSession::sendResponse(HttpResponse::ptr response)
        {
            // 步骤1：将 HttpResponse 对象序列化为完整的 HTTP 响应报文字符串
            // 包括状态行、响应头、空行、响应体
            std::string data = response->toString();
            
            // 步骤2：通过 socket 发送完整报文
            // writeFixSize 保证写入指定长度的数据（循环写直到全部发送完毕）
            // 返回值：成功返回写入字节数，失败返回 -1
            return writeFixSize(data.c_str(), data.size());
        }

    } // namespace http
} // namespace sylar

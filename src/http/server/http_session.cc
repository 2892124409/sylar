#include "http/server/http_session.h"

namespace http
{

    HttpSession::HttpSession(Socket::ptr sock, bool owner)
        : SocketStream(sock, owner)
    {
    }

    HttpRequest::ptr HttpSession::recvRequest()
    {
        // 第五阶段改为委托 HttpContext 管理连接级解析状态。
        return m_context.recvRequest(*this);
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

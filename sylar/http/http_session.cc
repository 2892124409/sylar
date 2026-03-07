#include "sylar/http/http_session.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner) {
}

HttpRequest::ptr HttpSession::recvRequest() {
    while (true) {
        size_t consumed = 0;
        HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);
        if (request) {
            m_buffer.erase(0, consumed);
            return request;
        }
        if (m_parser.hasError()) {
            return HttpRequest::ptr();
        }

        char buffer[4096];
        int rt = read(buffer, sizeof(buffer));
        if (rt <= 0) {
            return HttpRequest::ptr();
        }
        m_buffer.append(buffer, rt);
    }
}

int HttpSession::sendResponse(HttpResponse::ptr response) {
    std::string data = response->toString();
    return writeFixSize(data.c_str(), data.size());
}

} // namespace http
} // namespace sylar

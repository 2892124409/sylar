#include "http/server/http_session.h"

#include <fstream>
#include <sys/stat.h>

namespace http
{

    HttpSession::HttpSession(sylar::Socket::ptr sock, bool owner)
        : sylar::SocketStream(sock, owner)
    {
    }

    HttpRequest::ptr HttpSession::recvRequest()
    {
        // 第五阶段改为委托 HttpContext 管理连接级解析状态。
        return m_context.recvRequest(*this);
    }

    int HttpSession::sendResponse(HttpResponse::ptr response)
    {
        // 流式响应：只发送 header，body 由业务代码自行写入
        if (response->isStream()) {
            std::string header = response->toHeaderString();
            return writeFixSize(header.data(), header.size());
        }

        // 普通响应：分离发送 header 和 body，减少内存拷贝
        std::string header = response->toHeaderString();
        int ret = writeFixSize(header.data(), header.size());
        if (ret <= 0) {
            return ret;  // header 发送失败
        }

        const std::string& body = response->getBody();
        if (!body.empty()) {
            int body_ret = writeFixSize(body.data(), body.size());
            if (body_ret <= 0) {
                return body_ret;  // body 发送失败
            }
            return ret + body_ret;  // 返回总发送字节数
        }

        return ret;  // 只有 header，无 body
    }

    int HttpSession::sendFile(const std::string &file_path, size_t chunk_size)
    {
        // 打开文件
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return -1;  // 文件打开失败
        }

        // 分块读取并发送
        char *buffer = new char[chunk_size];
        int64_t total_sent = 0;

        while (file.read(buffer, chunk_size) || file.gcount() > 0) {
            size_t bytes_read = file.gcount();
            int ret = writeFixSize(buffer, bytes_read);
            if (ret <= 0) {
                delete[] buffer;
                return ret;  // 发送失败
            }
            total_sent += ret;
        }

        delete[] buffer;
        return total_sent;
    }

} // namespace http

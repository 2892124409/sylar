#include "sylar/http/http_server.h"
#include "sylar/http/http_parser.h"
#include "sylar/net/address.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"
#include "sylar/fiber/hook.h"
#include "sylar/log/logger.h"

#include <cassert>
#include <string>
#include <vector>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

int main() {
    sylar::set_hook_enable(true);
    sylar::IOManager iom(2, true, "http_test");
    sylar::http::HttpServer::ptr server(new sylar::http::HttpServer(&iom, &iom));
    server->getServletDispatch()->addServlet("/ping", [](sylar::http::HttpRequest::ptr,
                                                        sylar::http::HttpResponse::ptr rsp,
                                                        sylar::http::HttpSession::ptr) {
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("pong");
        return 0;
    });

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny("127.0.0.1:28080"));
    assert(server->bind(addrs, fails));
    assert(server->start());

    iom.schedule([]() {
        sleep(1);
        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        assert(sock->connect(sylar::Address::LookupAny("127.0.0.1:28080")));
        sylar::SocketStream stream(sock);
        std::string req = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        assert(stream.writeFixSize(req.c_str(), req.size()) == (int)req.size());
        char buf[1024] = {0};
        int rt = stream.read(buf, sizeof(buf));
        assert(rt > 0);
        std::string rsp(buf, rt);
        assert(rsp.find("200 OK") != std::string::npos);
        assert(rsp.find("pong") != std::string::npos);
        stream.close();
    });

    iom.schedule([]() {
        sleep(2);
        size_t old_header = sylar::http::HttpRequestParser::GetMaxHeaderSize();
        sylar::http::HttpRequestParser::SetMaxHeaderSize(32);

        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        assert(sock->connect(sylar::Address::LookupAny("127.0.0.1:28080")));
        sylar::SocketStream stream(sock);
        std::string req = "GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Long: 1234567890\r\n\r\n";
        assert(stream.writeFixSize(req.c_str(), req.size()) == (int)req.size());
        char buf[1024] = {0};
        int rt = stream.read(buf, sizeof(buf));
        assert(rt > 0);
        std::string rsp(buf, rt);
        assert(rsp.find("413 Payload Too Large") != std::string::npos);
        stream.close();

        sylar::http::HttpRequestParser::SetMaxHeaderSize(old_header);
    });

    iom.schedule([server]() {
        sleep(3);
        server->stop();
    });

    SYLAR_LOG_INFO(g_logger) << "test_http_server passed";
    return 0;
}

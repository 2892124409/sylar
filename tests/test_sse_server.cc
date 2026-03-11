#include "http/server/http_server.h"
#include "http/stream/sse.h"
#include "sylar/net/address.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"
#include "sylar/fiber/hook.h"
#include "log/logger.h"

#include <assert.h>
#include <string>
#include <vector>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

int main()
{
    sylar::set_hook_enable(true);
    sylar::IOManager iom(2, true, "sse_test");
    http::HttpServer::ptr server(new http::HttpServer(&iom, &iom));
    server->getServletDispatch()->addServlet("/events", [](http::HttpRequest::ptr,
                                                           http::HttpResponse::ptr rsp,
                                                           http::HttpSession::ptr session)
                                             {
        rsp->setStatus(http::HttpStatus::OK);
        rsp->setKeepAlive(false);
        rsp->setStream(true);
        rsp->setHeader("Content-Type", "text/event-stream");
        rsp->setHeader("Cache-Control", "no-cache");

        std::string headers = rsp->toHeaderString();
        if (session->writeFixSize(headers.c_str(), headers.size()) <= 0) {
            return -1;
        }

        http::SSEWriter writer(session);
        if (writer.sendComment("ping") <= 0) {
            return -1;
        }
        if (writer.sendEvent("hello\nworld", "message", "evt-1", 1000) <= 0) {
            return -1;
        }
        return 0; });

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny("127.0.0.1:28081"));
    assert(server->bind(addrs, fails));
    assert(server->start());

    iom.schedule([]()
                 {
        sleep(1);
        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        assert(sock->connect(sylar::Address::LookupAny("127.0.0.1:28081")));
        sylar::SocketStream stream(sock);
        std::string req = "GET /events HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        assert(stream.writeFixSize(req.c_str(), req.size()) == (int)req.size());

        std::string rsp;
        char buf[1024];
        int rt = 0;
        while ((rt = stream.read(buf, sizeof(buf))) > 0) {
            rsp.append(buf, rt);
        }

        assert(rsp.find("HTTP/1.1 200 OK\r\n") != std::string::npos);
        assert(rsp.find("content-type: text/event-stream\r\n") != std::string::npos);
        assert(rsp.find("cache-control: no-cache\r\n") != std::string::npos);
        assert(rsp.find("connection: close\r\n") != std::string::npos);
        assert(rsp.find(": ping\n\n") != std::string::npos);
        assert(rsp.find("event: message\n") != std::string::npos);
        assert(rsp.find("id: evt-1\n") != std::string::npos);
        assert(rsp.find("retry: 1000\n") != std::string::npos);
        assert(rsp.find("data: hello\n") != std::string::npos);
        assert(rsp.find("data: world\n\n") != std::string::npos);
        stream.close(); });

    iom.schedule([server]()
                 {
        sleep(2);
        server->stop(); });

    BASE_LOG_INFO(g_logger) << "test_sse_server passed";
    return 0;
}

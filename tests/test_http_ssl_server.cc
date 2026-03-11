#include "sylar/http/http_server.h"
#include "sylar/http/ssl/ssl_config.h"
#include "sylar/http/ssl/ssl_context.h"
#include "sylar/http/ssl/ssl_socket.h"
#include "sylar/http/ssl/ssl_socket_stream.h"
#include "sylar/net/address.h"
#include "sylar/fiber/hook.h"
#include "sylar/log/logger.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace
{

    std::string PrepareSslFixtures()
    {
        char tmpl[] = "/tmp/sylar_http_ssl_XXXXXX";
        char *dir = mkdtemp(tmpl);
        assert(dir);
        std::string base(dir);
        std::string cert = base + "/server.crt";
        std::string key = base + "/server.key";
        std::string cmd = "openssl req -x509 -nodes -newkey rsa:2048 -keyout \"" + key +
                          "\" -out \"" + cert +
                          "\" -subj /CN=localhost -days 1 >/dev/null 2>&1";
        assert(std::system(cmd.c_str()) == 0);
        return base;
    }

} // namespace

int main()
{
    sylar::set_hook_enable(true);
    std::string fixture_dir = PrepareSslFixtures();

    sylar::IOManager iom(2, true, "http_ssl_test");
    sylar::http::HttpServer::ptr server(new sylar::http::HttpServer(&iom, &iom));
    sylar::http::ssl::SslConfig config;
    config.setCertificateFile(fixture_dir + "/server.crt");
    config.setPrivateKeyFile(fixture_dir + "/server.key");
    assert(server->setSslConfig(config));

    server->getServletDispatch()->addServlet("/ping", [](sylar::http::HttpRequest::ptr,
                                                         sylar::http::HttpResponse::ptr rsp,
                                                         sylar::http::HttpSession::ptr) {
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("secure-pong");
        return 0;
    });

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny("127.0.0.1:28443"));
    assert(server->bind(addrs, fails));
    assert(server->start());

    iom.schedule([]() {
        sleep(1);
        sylar::http::ssl::SslConfig client_config;
        sylar::http::ssl::SslContext::ptr client_ctx(new sylar::http::ssl::SslContext(client_config, sylar::http::ssl::SslMode::CLIENT));
        assert(client_ctx->initialize());
        sylar::http::ssl::SslSocket::ptr sock = sylar::http::ssl::SslSocket::CreateTCPSocket(client_ctx, sylar::http::ssl::SslMode::CLIENT);
        assert(sock->connect(sylar::Address::LookupAny("127.0.0.1:28443")));
        sylar::http::ssl::SslSocketStream stream(sock);
        std::string req = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        assert(stream.writeFixSize(req.c_str(), req.size()) == static_cast<int>(req.size()));
        char buf[1024] = {0};
        int rt = stream.read(buf, sizeof(buf));
        assert(rt > 0);
        std::string rsp(buf, rt);
        assert(rsp.find("200 OK") != std::string::npos);
        assert(rsp.find("secure-pong") != std::string::npos);
        stream.close();
    });

    iom.schedule([server]() {
        sleep(2);
        server->stop();
    });

    SYLAR_LOG_INFO(g_logger) << "test_http_ssl_server passed";
    return 0;
}

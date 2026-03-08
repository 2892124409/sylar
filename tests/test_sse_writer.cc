#include "sylar/http/sse.h"
#include "sylar/net/socket.h"
#include "sylar/log/logger.h"

#include <assert.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

class TestSocket : public sylar::Socket {
public:
    TestSocket(int family, int type, int protocol = 0)
        : sylar::Socket(family, type, protocol) {
    }

    using sylar::Socket::init;
};

static sylar::http::HttpSession::ptr CreateSessionFromFd(int fd) {
    std::shared_ptr<TestSocket> sock(new TestSocket(AF_UNIX, SOCK_STREAM, 0));
    assert(sock->init(fd));
    return sylar::http::HttpSession::ptr(new sylar::http::HttpSession(sock));
}

void test_send_event() {
    int fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    sylar::http::HttpSession::ptr session = CreateSessionFromFd(fds[0]);
    sylar::http::SSEWriter writer(session);
    assert(writer.sendEvent("hello\nworld", "message", "evt-1", 1500) > 0);

    char buf[256] = {0};
    int rt = ::read(fds[1], buf, sizeof(buf));
    assert(rt > 0);
    std::string payload(buf, rt);
    assert(payload.find("event: message\n") != std::string::npos);
    assert(payload.find("id: evt-1\n") != std::string::npos);
    assert(payload.find("retry: 1500\n") != std::string::npos);
    assert(payload.find("data: hello\n") != std::string::npos);
    assert(payload.find("data: world\n\n") != std::string::npos);

    session->close();
    ::close(fds[1]);
}

void test_send_comment() {
    int fds[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    sylar::http::HttpSession::ptr session = CreateSessionFromFd(fds[0]);
    sylar::http::SSEWriter writer(session);
    assert(writer.sendComment("ping") > 0);

    char buf[64] = {0};
    int rt = ::read(fds[1], buf, sizeof(buf));
    assert(rt > 0);
    std::string payload(buf, rt);
    assert(payload == ": ping\n\n");

    session->close();
    ::close(fds[1]);
}

int main() {
    test_send_event();
    test_send_comment();
    SYLAR_LOG_INFO(g_logger) << "test_sse_writer passed";
    return 0;
}

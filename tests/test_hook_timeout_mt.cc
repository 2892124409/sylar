#include "sylar/fiber/fd_manager.h"
#include "sylar/fiber/hook.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/base/util.h"

#include <assert.h>
#include <atomic>
#include <errno.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int main()
{
    sylar::set_hook_enable(true);

    const int task_count = 64;
    std::atomic<int> completed(0);
    std::atomic<int> timed_out(0);
    std::atomic<int> other_err(0);
    std::atomic<int> first_errno(0);

    {
        sylar::IOManager iom(4, false, "hook_timeout_mt");

        for (int i = 0; i < task_count; ++i)
        {
            int sock_pair[2] = {-1, -1};
            int rt = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sock_pair);
            assert(rt == 0);
            (void)rt;

            sylar::FdMgr::GetInstance()->get(sock_pair[0], true);
            sylar::FdMgr::GetInstance()->get(sock_pair[1], true);

            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000;
            rt = setsockopt(sock_pair[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            assert(rt == 0);
            (void)rt;

            const int read_fd = sock_pair[0];
            const int write_fd = sock_pair[1];

            iom.schedule([read_fd, write_fd, &completed, &timed_out, &other_err, &first_errno]()
                         {
                char buffer[8] = {0};
                ssize_t n = recv(read_fd, buffer, sizeof(buffer), 0);

                if (n == -1 && errno == ETIMEDOUT)
                {
                    timed_out.fetch_add(1);
                }
                else
                {
                    other_err.fetch_add(1);
                    int expected = 0;
                    first_errno.compare_exchange_strong(expected, errno);
                    std::cerr << "unexpected recv result n=" << n
                              << " errno=" << errno
                              << " thread=" << sylar::GetThreadId()
                              << std::endl;
                }

                completed.fetch_add(1);
                close(read_fd);
                close(write_fd); });
        }

        uint64_t deadline = sylar::GetCurrentMS() + 3000;
        while (sylar::GetCurrentMS() < deadline && completed.load() < task_count)
        {
            ::usleep(20 * 1000);
        }

        if (completed.load() != task_count || other_err.load() != 0 || timed_out.load() != task_count)
        {
            std::cerr << "completed=" << completed.load()
                      << " timed_out=" << timed_out.load()
                      << " other_err=" << other_err.load()
                      << " first_errno=" << first_errno.load()
                      << std::endl;
        }

        assert(completed.load() == task_count);
        assert(other_err.load() == 0);
        assert(timed_out.load() == task_count);
    }

    return 0;
}

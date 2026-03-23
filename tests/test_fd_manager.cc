#include "sylar/fiber/fd_manager.h"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

int main()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sylar::FdCtx::ptr first = sylar::FdMgr::GetInstance()->get(fd, true);
    assert(first);
    assert(first->isSocket());
    assert(!first->isClose());

    // 模拟 close() 与 del() 之间的短窗口：旧上下文已标记关闭，但槽位尚未清空。
    first->setClose(true);

    sylar::FdCtx::ptr second = sylar::FdMgr::GetInstance()->get(fd, true);
    assert(second);
    assert(second.get() != first.get());
    assert(second->isSocket());
    assert(!second->isClose());

    sylar::FdMgr::GetInstance()->del(fd);
    ::close(fd);

    std::cout << "test_fd_manager passed" << std::endl;
    return 0;
}

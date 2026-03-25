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

    sylar::FdCtx::ptr second;
#ifdef SYLAR_NET_VARIANT_UPSTREAM_REF
    // upstream_ref 仅验证 del 后可重建上下文（无 setClose 语义）。
    sylar::FdMgr::GetInstance()->del(fd);
    second = sylar::FdMgr::GetInstance()->get(fd, true);
#else
    // 模拟 close() 与 del() 之间的短窗口：旧上下文已标记关闭，但槽位尚未清空。
    first->setClose(true);
    second = sylar::FdMgr::GetInstance()->get(fd, true);
#endif
    assert(second);
    assert(second.get() != first.get());
    assert(second->isSocket());
    assert(!second->isClose());

    sylar::FdMgr::GetInstance()->del(fd);
    ::close(fd);

    std::cout << "test_fd_manager passed" << std::endl;
    return 0;
}

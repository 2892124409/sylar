#ifndef __SYLAR_COMPAT_UPSTREAM_REF_NET_TCP_SERVER_H__
#define __SYLAR_COMPAT_UPSTREAM_REF_NET_TCP_SERVER_H__

#include "sylar_upstream_ref/tcp_server.h"

namespace sylar
{
namespace net
{

class TcpServer : public sylar::TcpServer
{
public:
    typedef std::shared_ptr<TcpServer> ptr;

    TcpServer(sylar::IOManager* io_worker = sylar::IOManager::GetThis(),
              sylar::IOManager* accept_worker = sylar::IOManager::GetThis())
        : sylar::TcpServer(io_worker, io_worker, accept_worker)
    {
    }
};

} // namespace net
} // namespace sylar

#endif

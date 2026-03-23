#ifndef __SYLAR_HTTP_SSL_SSL_TYPES_H__
#define __SYLAR_HTTP_SSL_SSL_TYPES_H__

namespace http
{
namespace ssl
{

/**
 * @brief SSL/TLS 工作模式
 */
enum class SslMode
{
    CLIENT = 0, ///< 客户端模式（SSL_connect）
    SERVER = 1  ///< 服务端模式（SSL_accept）
};

} // namespace ssl
} // namespace http

#endif

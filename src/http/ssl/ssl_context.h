#ifndef __SYLAR_HTTP_SSL_SSL_CONTEXT_H__
#define __SYLAR_HTTP_SSL_SSL_CONTEXT_H__

#include "http/ssl/ssl_config.h"
#include "http/ssl/ssl_types.h"

#include <memory>

typedef struct ssl_ctx_st SSL_CTX;

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            class SslContext
            {
            public:
                typedef std::shared_ptr<SslContext> ptr;

                SslContext(const SslConfig &config, SslMode mode);
                ~SslContext();

                bool initialize();

                SSL_CTX *getNativeHandle() const { return m_ctx; }
                SslMode getMode() const { return m_mode; }

            private:
                SslConfig m_config;
                SslMode m_mode;
                SSL_CTX *m_ctx;
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif

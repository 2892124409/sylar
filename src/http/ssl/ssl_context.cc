#include "http/ssl/ssl_context.h"

#include "log/logger.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

            namespace
            {

                static void InitOpenSSL()
                {
                    static bool s_inited = false;
                    if (!s_inited)
                    {
                        SSL_library_init();
                        SSL_load_error_strings();
                        OpenSSL_add_all_algorithms();
                        s_inited = true;
                    }
                }

            } // namespace

            SslContext::SslContext(const SslConfig &config, SslMode mode)
                : m_config(config)
                , m_mode(mode)
                , m_ctx(nullptr)
            {
            }

            SslContext::~SslContext()
            {
                if (m_ctx)
                {
                    SSL_CTX_free(m_ctx);
                    m_ctx = nullptr;
                }
            }

            bool SslContext::initialize()
            {
                InitOpenSSL();

                const SSL_METHOD *method = (m_mode == SslMode::SERVER) ? TLS_server_method() : TLS_client_method();
                m_ctx = SSL_CTX_new(method);
                if (!m_ctx)
                {
                    SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_new failed";
                    return false;
                }

                SSL_CTX_set_mode(m_ctx, SSL_MODE_AUTO_RETRY);

                if (m_mode == SslMode::SERVER)
                {
                    if (!m_config.isServerConfigReady())
                    {
                        SYLAR_LOG_ERROR(g_logger) << "SSL server config incomplete";
                        return false;
                    }
                    if (SSL_CTX_use_certificate_file(m_ctx, m_config.getCertificateFile().c_str(), SSL_FILETYPE_PEM) != 1)
                    {
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_certificate_file failed: " << m_config.getCertificateFile();
                        return false;
                    }
                    if (SSL_CTX_use_PrivateKey_file(m_ctx, m_config.getPrivateKeyFile().c_str(), SSL_FILETYPE_PEM) != 1)
                    {
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_PrivateKey_file failed: " << m_config.getPrivateKeyFile();
                        return false;
                    }
                    if (SSL_CTX_check_private_key(m_ctx) != 1)
                    {
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_check_private_key failed";
                        return false;
                    }
                }

                if (!m_config.getCaFile().empty())
                {
                    if (SSL_CTX_load_verify_locations(m_ctx, m_config.getCaFile().c_str(), nullptr) != 1)
                    {
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_load_verify_locations failed: " << m_config.getCaFile();
                        return false;
                    }
                }

                SSL_CTX_set_verify(m_ctx, m_config.getVerifyPeer() ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
                return true;
            }

        } // namespace ssl
    } // namespace http
} // namespace sylar

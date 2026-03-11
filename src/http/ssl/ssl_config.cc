#include "http/ssl/ssl_config.h"

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            SslConfig::SslConfig()
                : m_verifyPeer(false)
            {
            }

            bool SslConfig::isServerConfigReady() const
            {
                return !m_certificateFile.empty() && !m_privateKeyFile.empty();
            }

        } // namespace ssl
    } // namespace http
} // namespace sylar

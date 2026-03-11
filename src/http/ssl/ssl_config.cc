#include "http/ssl/ssl_config.h"

namespace http
{
    namespace ssl
    {

        /** @brief 构造默认 SSL 配置 */
        SslConfig::SslConfig()
            // 默认不开启对端证书校验，便于服务端单向 TLS 起步。
            : m_verifyPeer(false)
        {
            // 其余字符串成员默认构造为空串。
        }

        /** @brief 判断服务端配置是否可用 */
        bool SslConfig::isServerConfigReady() const
        {
            // 服务端最小可用条件：证书与私钥路径都已设置。
            return !m_certificateFile.empty() && !m_privateKeyFile.empty();
        }

    } // namespace ssl
} // namespace http

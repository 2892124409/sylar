#ifndef __SYLAR_HTTP_SSL_SSL_CONFIG_H__
#define __SYLAR_HTTP_SSL_SSL_CONFIG_H__

#include <memory>
#include <string>

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            /**
             * @brief SSL/TLS 初始化配置
             * @details
             * 保存证书、私钥、CA 以及对端证书校验策略。
             */
            class SslConfig
            {
            public:
                typedef std::shared_ptr<SslConfig> ptr;

                /** @brief 构造默认配置 */
                SslConfig();

                /** @brief 获取证书文件路径 */
                const std::string &getCertificateFile() const { return m_certificateFile; }
                /** @brief 设置证书文件路径 */
                void setCertificateFile(const std::string &value) { m_certificateFile = value; }

                /** @brief 获取私钥文件路径 */
                const std::string &getPrivateKeyFile() const { return m_privateKeyFile; }
                /** @brief 设置私钥文件路径 */
                void setPrivateKeyFile(const std::string &value) { m_privateKeyFile = value; }

                /** @brief 获取 CA 文件路径 */
                const std::string &getCaFile() const { return m_caFile; }
                /** @brief 设置 CA 文件路径 */
                void setCaFile(const std::string &value) { m_caFile = value; }

                /** @brief 获取是否校验对端证书 */
                bool getVerifyPeer() const { return m_verifyPeer; }
                /** @brief 设置是否校验对端证书 */
                void setVerifyPeer(bool value) { m_verifyPeer = value; }

                /**
                 * @brief 判断服务端配置是否可用
                 * @return 证书和私钥均已配置返回 true
                 */
                bool isServerConfigReady() const;

            private:
                std::string m_certificateFile; ///< 服务端证书路径（PEM）
                std::string m_privateKeyFile;  ///< 服务端私钥路径（PEM）
                std::string m_caFile;          ///< CA 证书路径（可选）
                bool m_verifyPeer;             ///< 是否校验对端证书
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif

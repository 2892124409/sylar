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

            class SslConfig
            {
            public:
                typedef std::shared_ptr<SslConfig> ptr;

                SslConfig();

                const std::string &getCertificateFile() const { return m_certificateFile; }
                void setCertificateFile(const std::string &value) { m_certificateFile = value; }

                const std::string &getPrivateKeyFile() const { return m_privateKeyFile; }
                void setPrivateKeyFile(const std::string &value) { m_privateKeyFile = value; }

                const std::string &getCaFile() const { return m_caFile; }
                void setCaFile(const std::string &value) { m_caFile = value; }

                bool getVerifyPeer() const { return m_verifyPeer; }
                void setVerifyPeer(bool value) { m_verifyPeer = value; }

                bool isServerConfigReady() const;

            private:
                std::string m_certificateFile;
                std::string m_privateKeyFile;
                std::string m_caFile;
                bool m_verifyPeer;
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif

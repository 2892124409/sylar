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

            /**
             * @brief SSL/TLS 上下文封装
             * @details
             * 管理 OpenSSL `SSL_CTX` 的创建、初始化与释放。
             */
            class SslContext
            {
            public:
                typedef std::shared_ptr<SslContext> ptr;

                /**
                 * @brief 构造上下文对象
                 * @param config SSL 配置
                 * @param mode   运行模式（CLIENT / SERVER）
                 */
                SslContext(const SslConfig &config, SslMode mode);

                /** @brief 析构并释放 `SSL_CTX` 资源 */
                ~SslContext();

                /**
                 * @brief 初始化 `SSL_CTX`
                 * @return true 成功；false 失败
                 */
                bool initialize();

                /** @brief 获取底层 OpenSSL `SSL_CTX*` 句柄 */
                SSL_CTX *getNativeHandle() const { return m_ctx; }

                /** @brief 获取当前工作模式 */
                SslMode getMode() const { return m_mode; }

            private:
                SslConfig m_config; ///< SSL 配置副本
                SslMode m_mode;     ///< 工作模式（客户端/服务端）
                SSL_CTX *m_ctx;     ///< OpenSSL 上下文句柄
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif

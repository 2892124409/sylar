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

            /** @brief 进程级 OpenSSL 全局初始化（只执行一次） */
            static void InitOpenSSL()
            {
                // 进程级静态标记：确保 OpenSSL 全局初始化逻辑只执行一次。
                static bool s_inited = false;
                // 若尚未初始化，执行完整初始化序列。
                if (!s_inited)
                {
                    // 初始化 SSL/TLS 核心库（历史 API，兼容 OpenSSL 1.0.x/1.1.x）。
                    SSL_library_init();
                    // 加载错误码到可读字符串映射，便于日志排障。
                    SSL_load_error_strings();
                    // 注册可用算法（摘要、加密、签名等）。
                    OpenSSL_add_all_algorithms();
                    // 标记初始化完成，后续调用将直接跳过。
                    s_inited = true;
                }
            }

            } // namespace

            /** @brief 构造函数：保存配置和模式，延迟到 initialize 创建上下文 */
            SslContext::SslContext(const SslConfig &config, SslMode mode)
                // 复制保存配置对象，避免依赖外部入参生命周期。
                : m_config(config)
                // 保存当前上下文模式（客户端或服务端）。
                , m_mode(mode)
                // 构造时不立即分配 SSL_CTX，延迟到 initialize()。
                , m_ctx(nullptr)
            {
                // 无额外运行时逻辑。
            }

            /** @brief 析构函数：释放 SSL_CTX */
            SslContext::~SslContext()
            {
                // 只有在上下文已创建时才释放。
                if (m_ctx)
                {
                    // 释放 OpenSSL 上下文资源。
                    SSL_CTX_free(m_ctx);
                    // 置空指针，避免悬垂访问。
                    m_ctx = nullptr;
                }
            }

            /** @brief 初始化 SSL_CTX 并加载证书/校验策略 */
            bool SslContext::initialize()
            {
                // 先确保 OpenSSL 全局环境可用。
                InitOpenSSL();

                // 根据上下文模式选择服务端或客户端方法族。
                const SSL_METHOD *method = (m_mode == SslMode::SERVER) ? TLS_server_method() : TLS_client_method();
                // 创建 SSL_CTX（后续所有连接会话都从这里派生）。
                m_ctx = SSL_CTX_new(method);
                // 创建失败通常意味着 OpenSSL 内部状态或内存问题。
                if (!m_ctx)
                {
                    // 输出关键错误日志，便于定位初始化失败点。
                    SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_new failed";
                    // 初始化失败，终止流程。
                    return false;
                }

                // 启用自动重试模式，降低部分阻塞 I/O 场景的调用复杂度。
                SSL_CTX_set_mode(m_ctx, SSL_MODE_AUTO_RETRY);

                // 服务端模式需要加载证书与私钥。
                if (m_mode == SslMode::SERVER)
                {
                    // 先做最小配置检查：证书和私钥路径是否都已设置。
                    if (!m_config.isServerConfigReady())
                    {
                        // 配置不完整时记录日志。
                        SYLAR_LOG_ERROR(g_logger) << "SSL server config incomplete";
                        // 终止初始化。
                        return false;
                    }

                    // 加载服务端证书（PEM）。
                    if (SSL_CTX_use_certificate_file(m_ctx, m_config.getCertificateFile().c_str(), SSL_FILETYPE_PEM) != 1)
                    {
                        // 证书加载失败时输出具体路径，便于快速排查文件问题。
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_certificate_file failed: " << m_config.getCertificateFile();
                        // 终止初始化。
                        return false;
                    }

                    // 加载服务端私钥（PEM）。
                    if (SSL_CTX_use_PrivateKey_file(m_ctx, m_config.getPrivateKeyFile().c_str(), SSL_FILETYPE_PEM) != 1)
                    {
                        // 私钥加载失败时输出具体路径。
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_use_PrivateKey_file failed: " << m_config.getPrivateKeyFile();
                        // 终止初始化。
                        return false;
                    }

                    // 校验证书与私钥是否匹配。
                    if (SSL_CTX_check_private_key(m_ctx) != 1)
                    {
                        // 不匹配时无法进行安全握手。
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_check_private_key failed";
                        // 终止初始化。
                        return false;
                    }
                }

                // 如果配置了 CA 文件，加载证书链校验来源。
                if (!m_config.getCaFile().empty())
                {
                    // 将 CA 证书加载到当前上下文。
                    if (SSL_CTX_load_verify_locations(m_ctx, m_config.getCaFile().c_str(), nullptr) != 1)
                    {
                        // 加载失败时输出路径信息。
                        SYLAR_LOG_ERROR(g_logger) << "SSL_CTX_load_verify_locations failed: " << m_config.getCaFile();
                        // 终止初始化。
                        return false;
                    }
                }

                // 设置对端证书校验策略：校验或不校验。
                SSL_CTX_set_verify(m_ctx, m_config.getVerifyPeer() ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
                // 全部步骤成功，返回 true。
                return true;
            }

        } // namespace ssl
    } // namespace http
} // namespace sylar

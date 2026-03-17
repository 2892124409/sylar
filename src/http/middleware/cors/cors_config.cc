#include "http/middleware/cors/cors_config.h"

// sylar 顶层命名空间。
namespace http
{
// cors 子命名空间。
namespace cors
{

/**
 * @brief 构造默认 CORS 配置
 */
CorsConfig::CorsConfig()
    // 默认允许常见 REST 方法和 OPTIONS 预检。
    : m_allowedMethods("GET, POST, PUT, PATCH, DELETE, OPTIONS")
      // 默认允许常见跨域请求头。
      ,
      m_allowedHeaders("Content-Type, Authorization, X-Requested-With")
      // 默认预检缓存 600 秒。
      ,
      m_maxAge(600)
      // 默认不允许凭证，避免与 '*' Origin 冲突。
      ,
      m_allowCredentials(false)
{
    // 默认允许所有来源。
    m_allowedOrigins.push_back("*");
}

/**
 * @brief 判断 Origin 是否匹配允许规则
 */
bool CorsConfig::isOriginAllowed(const std::string& origin) const
{
    // 遍历允许列表，支持 '*' 通配和精确 Origin 匹配。
    for (size_t i = 0; i < m_allowedOrigins.size(); ++i)
    {
        if (m_allowedOrigins[i] == "*" || m_allowedOrigins[i] == origin)
        {
            // 命中任一规则即允许。
            return true;
        }
    }
    // 未命中则拒绝。
    return false;
}

// 结束 cors 命名空间。
} // namespace cors
// 结束 http 命名空间。
} // namespace http

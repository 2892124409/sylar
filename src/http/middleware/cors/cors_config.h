#ifndef __SYLAR_HTTP_MIDDLEWARE_CORS_CONFIG_H__
#define __SYLAR_HTTP_MIDDLEWARE_CORS_CONFIG_H__

#include <string>
#include <vector>

namespace http
{
namespace cors
{

/**
 * @brief CORS 策略配置对象
 * @details
 * 该类只负责保存跨域策略参数，不参与请求分发流程。
 */
class CorsConfig
{
  public:
    /**
     * @brief 构造默认 CORS 策略
     * @details
     * 默认允许所有 Origin（*），默认方法/请求头为常见 REST 场景。
     */
    CorsConfig();

    /** @brief 获取允许的 Origin 列表 */
    const std::vector<std::string>& getAllowedOrigins() const
    {
        return m_allowedOrigins;
    }
    /** @brief 设置允许的 Origin 列表 */
    void setAllowedOrigins(const std::vector<std::string>& value)
    {
        m_allowedOrigins = value;
    }

    /** @brief 获取允许的方法列表（逗号分隔） */
    const std::string& getAllowedMethods() const
    {
        return m_allowedMethods;
    }
    /** @brief 设置允许的方法列表（逗号分隔） */
    void setAllowedMethods(const std::string& value)
    {
        m_allowedMethods = value;
    }

    /** @brief 获取允许的请求头列表（逗号分隔） */
    const std::string& getAllowedHeaders() const
    {
        return m_allowedHeaders;
    }
    /** @brief 设置允许的请求头列表（逗号分隔） */
    void setAllowedHeaders(const std::string& value)
    {
        m_allowedHeaders = value;
    }

    /** @brief 获取允许暴露给前端的响应头列表（逗号分隔） */
    const std::string& getExposeHeaders() const
    {
        return m_exposeHeaders;
    }
    /** @brief 设置允许暴露给前端的响应头列表（逗号分隔） */
    void setExposeHeaders(const std::string& value)
    {
        m_exposeHeaders = value;
    }

    /** @brief 获取预检缓存时间（秒） */
    int getMaxAge() const
    {
        return m_maxAge;
    }
    /** @brief 设置预检缓存时间（秒） */
    void setMaxAge(int value)
    {
        m_maxAge = value;
    }

    /** @brief 获取是否允许跨域携带凭证 */
    bool getAllowCredentials() const
    {
        return m_allowCredentials;
    }
    /** @brief 设置是否允许跨域携带凭证 */
    void setAllowCredentials(bool value)
    {
        m_allowCredentials = value;
    }

    /**
     * @brief 判断 Origin 是否允许
     * @param origin 请求头中的 Origin 值
     * @return true 允许；false 不允许
     */
    bool isOriginAllowed(const std::string& origin) const;

  private:
    std::vector<std::string> m_allowedOrigins; ///< 允许的 Origin 列表（可含 *）
    std::string m_allowedMethods;              ///< Access-Control-Allow-Methods
    std::string m_allowedHeaders;              ///< Access-Control-Allow-Headers
    std::string m_exposeHeaders;               ///< Access-Control-Expose-Headers
    int m_maxAge;                              ///< Access-Control-Max-Age（秒）
    bool m_allowCredentials;                   ///< 是否允许凭证
};

} // namespace cors
} // namespace http

#endif

#include "http/middleware/cors/cors_middleware.h"

// sylar 顶层命名空间。
namespace http
{
    // cors 子命名空间。
    namespace cors
    {

        /**
         * @brief 构造函数
         */
        CorsMiddleware::CorsMiddleware(const CorsConfig &config)
            // 复制保存配置，后续 before/after 直接使用。
            : m_config(config)
        {
        }

        /**
         * @brief 前置处理：OPTIONS 预检短路
         */
        bool CorsMiddleware::before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr)
        {
            // 先写入通用 CORS 头（若 Origin 合法）。
            applyCommonHeaders(request, response);
            // 非 OPTIONS 请求直接放行，继续执行业务处理。
            if (request->getMethod() != HttpMethod::OPTIONS)
            {
                return true;
            }

            // OPTIONS 预检请求：直接返回 204 并短路后续业务链路。
            response->setStatus(static_cast<HttpStatus>(204));
            // 显式原因短语，便于抓包和日志观察。
            response->setReason("No Content");
            // 告知浏览器允许的跨域方法集合。
            response->setHeader("Access-Control-Allow-Methods", m_config.getAllowedMethods());
            // 告知浏览器允许的跨域请求头集合。
            response->setHeader("Access-Control-Allow-Headers", m_config.getAllowedHeaders());
            // 告知浏览器预检结果可缓存的时长。
            response->setHeader("Access-Control-Max-Age", std::to_string(m_config.getMaxAge()));
            // 预检响应通常无实体。
            response->setBody("");
            // 返回 false：中断后续路由/业务处理。
            return false;
        }

        /**
         * @brief 后置处理：兜底补头
         */
        void CorsMiddleware::after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr)
        {
            // 后置阶段再次补齐通用头，覆盖短路/异常等路径。
            applyCommonHeaders(request, response);
            // 只有配置了 Expose-Headers 时才输出该响应头。
            if (!m_config.getExposeHeaders().empty())
            {
                response->setHeader("Access-Control-Expose-Headers", m_config.getExposeHeaders());
            }
        }

        /**
         * @brief 写入通用 CORS 头
         */
        void CorsMiddleware::applyCommonHeaders(HttpRequest::ptr request, HttpResponse::ptr response) const
        {
            // 读取请求来源域。
            std::string origin = request->getHeader("Origin");
            // 非跨域请求或来源不在白名单时不写 CORS 头。
            if (origin.empty() || !m_config.isOriginAllowed(origin))
            {
                return;
            }

            // 获取允许的来源策略。
            const std::vector<std::string> &allowed_origins = m_config.getAllowedOrigins();
            // '*' 且不允许凭证时，可直接返回通配来源。
            if (!allowed_origins.empty() && allowed_origins[0] == "*" && !m_config.getAllowCredentials())
            {
                response->setHeader("Access-Control-Allow-Origin", "*");
            }
            else
            {
                // 凭证场景下必须回显具体 Origin，不能返回 '*'.
                response->setHeader("Access-Control-Allow-Origin", origin);
                // 提示代理缓存按 Origin 维度区分响应。
                response->setHeader("Vary", "Origin");
            }

            // 允许凭证时追加对应标头。
            if (m_config.getAllowCredentials())
            {
                response->setHeader("Access-Control-Allow-Credentials", "true");
            }
        }

    // 结束 cors 命名空间。
    } // namespace cors
// 结束 http 命名空间。
} // namespace http

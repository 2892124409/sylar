#ifndef __SYLAR_HTTP_MIDDLEWARE_CORS_MIDDLEWARE_H__
#define __SYLAR_HTTP_MIDDLEWARE_CORS_MIDDLEWARE_H__

#include "http/middleware/middleware.h"
#include "http/middleware/cors/cors_config.h"

// sylar 顶层命名空间。
namespace sylar
{
    // http 子命名空间。
    namespace http
    {
        // cors 子命名空间。
        namespace cors
        {

            /**
             * @brief CORS 中间件
             * @details
             * 在请求前后统一处理 CORS 相关响应头，支持 OPTIONS 预检短路。
             */
            class CorsMiddleware : public Middleware
            {
            public:
                /// 智能指针别名。
                typedef std::shared_ptr<CorsMiddleware> ptr;

                /**
                 * @brief 构造 CORS 中间件
                 * @param config CORS 策略配置
                 */
                CorsMiddleware(const CorsConfig &config = CorsConfig());

                /**
                 * @brief 前置处理
                 * @details
                 * 非 OPTIONS 请求放行；OPTIONS 请求直接构造预检响应并短路。
                 */
                virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;
                /**
                 * @brief 后置处理
                 * @details 兜底补齐通用 CORS 头与 Expose-Headers。
                 */
                virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

            private:
                /**
                 * @brief 写入通用 CORS 响应头
                 */
                void applyCommonHeaders(HttpRequest::ptr request, HttpResponse::ptr response) const;

            private:
                CorsConfig m_config; ///< 当前中间件使用的 CORS 策略
            };

        } // namespace cors
    } // namespace http
} // namespace sylar

#endif

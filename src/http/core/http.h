#ifndef __SYLAR_HTTP_HTTP_H__
#define __SYLAR_HTTP_HTTP_H__

#include <stdint.h>
#include <string>

namespace http
{

    /**
     * @brief HTTP 请求方法枚举
     * @details
     * 这里只保留当前阶段会用到的常见方法。
     * 后续如果要支持更完整的 HTTP 语义，可以继续扩展。
     */
    enum class HttpMethod
    {
        /// 非法/暂不支持的方法，通常表示解析阶段未识别成功
        INVALID_METHOD = 0,
        /// 获取资源，最常见的读取型请求，例如 GET /ping
        GET,
        /// 提交数据，常用于表单提交、创建资源、发送 JSON
        POST,
        /// 整体更新资源，语义上通常表示“用新内容替换旧资源”
        PUT,
        /// 删除资源，因为 DELETE 作为名字有冲突风险，这里写成 DELETE_
        DELETE_,
        /// 只获取响应头，不要响应体，常用于探测资源是否存在
        HEAD,
        /// 获取资源支持的通信选项，常见于 CORS 预检请求
        OPTIONS,
        /// 局部更新资源，语义上通常表示“修改部分字段”
        PATCH
    };

    /**
     * @brief HTTP 响应状态码枚举
     * @details
     * 当前只保留框架开发阶段最常见的一小部分状态码，
     * 够支撑 ping/echo/session/sse 这些基础场景。
     */
    enum class HttpStatus
    {
        /// 200，请求成功，服务器正常返回结果
        OK = 200,
        /// 400，请求格式错误，常见于非法请求行、请求头或 body
        BAD_REQUEST = 400,
        /// 404，请求的路由或资源不存在
        NOT_FOUND = 404,
        /// 408，请求超时，服务器长时间未等到完整请求
        REQUEST_TIMEOUT = 408,
        /// 500，服务器内部错误，表示服务端处理逻辑异常
        INTERNAL_SERVER_ERROR = 500,
        /// 501，服务器暂未实现该能力，例如方法或功能未支持
        NOT_IMPLEMENTED = 501,
        /// 503，服务暂不可用，常用于过载、维护或依赖未就绪
        SERVICE_UNAVAILABLE = 503
    };

    /**
     * @brief 将 HttpMethod 转成标准字符串
     * @example HttpMethod::GET -> "GET"
     */
    std::string HttpMethodToString(HttpMethod method);

    /**
     * @brief 将方法字符串转成 HttpMethod 枚举
     * @details
     * 如果方法不在当前支持范围内，则返回 INVALID_METHOD。
     */
    HttpMethod StringToHttpMethod(const std::string &method);

    /**
     * @brief 将状态码转成默认 reason phrase
     * @example HttpStatus::OK -> "OK"
     */
    std::string HttpStatusToString(HttpStatus status);

} // namespace http

#endif

// 引入错误响应工具函数声明。
#include "http/core/http_error.h"

// 引入框架配置（用于读取错误输出格式：text/json）。
#include "http/core/http_framework_config.h"

// 引入字符串流，便于拼接 JSON 文本。
#include <sstream>

// sylar 顶层命名空间开始。
namespace http
{

// 匿名命名空间：仅本文件可见的内部工具函数。
namespace
{
// 对 JSON 字符串做最小必要转义，避免非法 JSON。
std::string EscapeJson(const std::string& value)
{
    // 使用字符串流累积转义后的结果。
    std::ostringstream ss;
    // 逐字符扫描输入文本。
    for (size_t i = 0; i < value.size(); ++i)
    {
        // 根据字符类型决定是否需要转义。
        switch (value[i])
        {
        // 反斜杠需要写成 \\。
        case '\\':
            ss << "\\\\";
            break;
        // 双引号需要写成 \"。
        case '"':
            ss << "\\\"";
            break;
        // 换行符转义为 \n。
        case '\n':
            ss << "\\n";
            break;
        // 回车符转义为 \r。
        case '\r':
            ss << "\\r";
            break;
        // 制表符转义为 \t。
        case '\t':
            ss << "\\t";
            break;
        // 其他普通字符原样输出。
        default:
            ss << value[i];
            break;
        }
    }
    // 返回转义后的字符串。
    return ss.str();
}
} // namespace

// 统一错误响应构造函数。
// 参数说明：
// - response: 要填充的响应对象
// - status: HTTP 状态码（如 400/404/500）
// - message: 错误主信息
// - details: 可选的细节信息
void ApplyErrorResponse(HttpResponse::ptr response,
                        HttpStatus status,
                        const std::string& message,
                        const std::string& details)
{
    // 设置 HTTP 状态码。
    response->setStatus(status);
    // 错误响应默认关闭连接，避免在异常路径继续复用连接。
    response->setKeepAlive(false);

    // 若配置为 JSON 错误输出格式。
    if (HttpFrameworkConfig::GetErrorResponseFormat() == HttpFrameworkConfig::ERROR_FORMAT_JSON)
    {
        // 设置 JSON Content-Type。
        response->setHeader("Content-Type", "application/json; charset=utf-8");
        // 用字符串流构造 JSON 文本。
        std::ostringstream ss;
        // JSON 对象开始。
        ss << "{";
        // 写 code 字段（数值型状态码）。
        ss << "\"code\":" << static_cast<int>(status) << ",";
        // 写 message 字段（做 JSON 转义）。
        ss << "\"message\":\"" << EscapeJson(message) << "\"";
        // details 非空时追加 details 字段。
        if (!details.empty())
        {
            // 写 details 字段（做 JSON 转义）。
            ss << ",\"details\":\"" << EscapeJson(details) << "\"";
        }
        // JSON 对象结束。
        ss << "}";
        // 设置响应体为构造好的 JSON。
        response->setBody(ss.str());
        // JSON 分支处理完成后直接返回。
        return;
    }

    // 非 JSON 分支：按纯文本输出。
    response->setHeader("Content-Type", "text/plain; charset=utf-8");
    // 若 details 为空只输出 message，否则 message + 换行 + details。
    response->setBody(details.empty() ? message : (message + "\n" + details));
}

} // namespace http

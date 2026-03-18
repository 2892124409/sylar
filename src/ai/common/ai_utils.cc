#include "ai/common/ai_utils.h"

#include "sylar/base/util.h"

#include <atomic>
#include <cstdlib>
#include <sstream>

/**
 * @file ai_utils.cc
 * @brief AI 应用层公共工具函数实现。
 */

namespace ai
{
namespace common
{

uint64_t NowMs()
{
    // 直接复用底层工具函数，返回当前毫秒时间戳。
    return sylar::GetCurrentMS();
}

std::string GenerateConversationId()
{
    // 进程内自增序列，保证同一毫秒下的 ID 仍可区分，注意静态变量只初始化一次。
    static std::atomic<uint64_t> s_id(0);
    // 使用字符串流拼接最终会话 ID 文本。
    std::ostringstream ss;
    // 格式：conv-<当前毫秒>-<自增序号>。
    ss << "conv-" << NowMs() << "-" << s_id.fetch_add(1, std::memory_order_relaxed);
    // 返回拼接结果。
    return ss.str();
}

std::string GenerateRequestId()
{
    // 独立于会话 ID 的请求级自增序列。
    static std::atomic<uint64_t> s_id(0);
    // 使用字符串流拼接 request_id。
    std::ostringstream ss;
    // 格式：<当前毫秒>-<自增序号>。
    ss << NowMs() << "-" << s_id.fetch_add(1, std::memory_order_relaxed);
    // 返回 request_id 字符串。
    return ss.str();
}

bool ParseJsonBody(http::HttpRequest::ptr request, nlohmann::json& out, std::string& error)
{
    // 防御式判断：请求对象为空直接返回错误。
    if (!request)
    {
        // 写入可读错误信息，便于上层统一输出。
        error = "request is null";
        // 解析失败。
        return false;
    }

    // 读取 HTTP 请求体原文。
    const std::string& body = request->getBody();
    // 请求体为空，无法解析 JSON。
    if (body.empty())
    {
        // 写入错误原因。
        error = "request body is empty";
        // 解析失败。
        return false;
    }

    // 关闭异常模式解析 JSON；失败时通过 is_discarded 判断。
    out = nlohmann::json::parse(body, nullptr, false);
    // 非法 JSON 进入该分支。
    if (out.is_discarded())
    {
        // 写入错误原因。
        error = "invalid json body";
        // 解析失败。
        return false;
    }

    // 解析成功。
    return true;
}

uint32_t ParseLimit(const std::string& text, uint32_t default_value, uint32_t max_value)
{
    // 未提供 limit 时直接使用默认值。
    if (text.empty())
    {
        return default_value;
    }

    // strtoul 的结束指针，用于校验是否完整解析成功。
    char* end = nullptr;
    // 按十进制把文本转为无符号整数。
    unsigned long value = std::strtoul(text.c_str(), &end, 10);
    // 解析失败、存在非法尾字符或值为 0 都回退默认值。
    if (!end || *end != '\0' || value == 0)
    {
        return default_value;
    }

    // 超过上限时进行截断保护。
    if (value > max_value)
    {
        return max_value;
    }

    // 在安全范围内，转换为 uint32_t 返回。
    return static_cast<uint32_t>(value);
}

void WriteJson(http::HttpResponse::ptr response,
               const nlohmann::json& body,
               http::HttpStatus status,
               const std::string& reason)
{
    // 设置 HTTP 状态码。
    response->setStatus(status);
    // 可选设置 reason phrase（不传则使用框架默认 reason）。
    if (!reason.empty())
    {
        response->setReason(reason);
    }
    // 明确声明 JSON 响应类型与 UTF-8 编码。
    response->setHeader("Content-Type", "application/json; charset=utf-8");
    // 序列化 JSON 对象并写入响应体。
    response->setBody(body.dump());
}

void WriteJsonError(http::HttpResponse::ptr response,
                    http::HttpStatus status,
                    const std::string& message,
                    const std::string& request_id,
                    const std::string& details)
{
    // 构建统一错误响应 JSON 对象。
    nlohmann::json error;
    // 统一标记失败。
    error["ok"] = false;
    // 输出 HTTP 状态码数值，便于客户端统一处理。
    error["code"] = static_cast<int>(status);
    // 输出可读错误消息。
    error["message"] = message;
    // 仅在有细节时附加 details 字段。
    if (!details.empty())
    {
        error["details"] = details;
    }
    // 仅在存在 request_id 时回传，便于链路追踪。
    if (!request_id.empty())
    {
        error["request_id"] = request_id;
    }

    // 复用统一 JSON 输出函数写回响应。
    WriteJson(response, error, status);
}

} // namespace common
} // namespace ai

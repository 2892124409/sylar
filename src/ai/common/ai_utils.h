#ifndef __SYLAR_AI_COMMON_AI_UTILS_H__
#define __SYLAR_AI_COMMON_AI_UTILS_H__

#include "http/core/http_request.h"
#include "http/core/http_response.h"

#include <stdint.h>

#include <string>

#include <nlohmann/json.hpp>

/**
 * @file ai_utils.h
 * @brief AI 应用层公共工具函数声明。
 *
 * 该模块负责：
 * - 时间与 ID 生成；
 * - JSON 请求体解析；
 * - 标准 JSON 响应/错误响应输出。
 */

namespace ai
{
namespace common
{

/**
 * @brief 获取当前时间（毫秒时间戳）。
 */
uint64_t NowMs();

/**
 * @brief 生成会话 ID。
 * @return 形如 `conv-<ms>-<seq>` 的字符串。
 */
std::string GenerateConversationId();

/**
 * @brief 生成请求链路 ID。
 * @return 形如 `<ms>-<seq>` 的字符串。
 */
std::string GenerateRequestId();

/**
 * @brief 解析 HTTP 请求体为 JSON。
 * @param request HTTP 请求对象。
 * @param[out] out 解析后的 JSON 对象。
 * @param[out] error 失败原因。
 * @return true 解析成功；false 解析失败。
 */
bool ParseJsonBody(http::HttpRequest::ptr request, nlohmann::json& out, std::string& error);

/**
 * @brief 解析 limit 参数并执行默认值/上限保护。
 * @param text 原始文本参数。
 * @param default_value 默认值。
 * @param max_value 允许的最大值。
 * @return 归一化后的 limit。
 */
uint32_t ParseLimit(const std::string& text, uint32_t default_value, uint32_t max_value);

/**
 * @brief 输出标准 JSON 响应。
 * @param response HTTP 响应对象。
 * @param body JSON 响应体。
 * @param status HTTP 状态码（默认 200）。
 * @param reason 可选自定义 reason phrase。
 */
void WriteJson(http::HttpResponse::ptr response,
               const nlohmann::json& body,
               http::HttpStatus status = http::HttpStatus::OK,
               const std::string& reason = "");

/**
 * @brief 输出统一 JSON 错误响应。
 * @details 统一结构：`ok/code/message/details/request_id`。
 * @param response HTTP 响应对象。
 * @param status HTTP 状态码。
 * @param message 错误消息。
 * @param request_id 请求链路 ID。
 * @param details 可选错误细节。
 */
void WriteJsonError(http::HttpResponse::ptr response,
                    http::HttpStatus status,
                    const std::string& message,
                    const std::string& request_id,
                    const std::string& details = "");

} // namespace common
} // namespace ai

#endif

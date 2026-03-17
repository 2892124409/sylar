#ifndef __SYLAR_AI_COMMON_AI_TYPES_H__
#define __SYLAR_AI_COMMON_AI_TYPES_H__

#include <stdint.h>

#include <string>
#include <vector>

/**
 * @file ai_types.h
 * @brief AI 应用层通用数据契约定义。
 *
 * 该文件被 API、Service、LLM、Storage 多层共享，用于统一请求/响应/持久化的数据形态。
 */

namespace ai
{
namespace common
{

/**
 * @brief 对话中的单条消息对象。
 * @details
 * 两个用处：
 * 1、作为上下文喂给模型
 * 2、作为历史接口返回给客户端
 */
struct ChatMessage
{
    /** @brief 消息角色（谁说的），例如 "user"、"assistant"、"system"。 */
    std::string role;
    /** @brief 消息文本内容。 */
    std::string content;
    /** @brief 消息创建时间（毫秒时间戳）。 */
    uint64_t created_at_ms = 0;
};

/**
 * @brief 聊天补全请求对象（应用层统一入参）。
 * @details
 * 该对象由 API 层从 HTTP 入参构建，并交给 ChatService 消费。
 * 表示这次对话请求要让服务干什么
 */
struct ChatCompletionRequest
{
    /**
     * @brief 会话标识 SID，标识这个客户端是谁（会话主体）。
     * @details
     * 当前项目的 SID 是从 Cookie 取的，一个浏览器如果不清理 Cookie 通常 SID 不变
     */
    std::string sid;
    /**
     * @brief 可选会话 ID；为空时由服务层创建新会话。
     * @details
     * 标识“这个主体下的第几条对话线程”
     * 一个 SID 可以有多个 conversation_id
     * 上下文是通过 sid#conversation_id 区分的
     */
    std::string conversation_id;
    /** @brief 当前用户输入内容（必填）。 */
    std::string message;

    /** @brief 模型名称。 */
    std::string model;
    /** @brief 采样温度参数。 */
    double temperature = 0.7;
    /** @brief 期望模型生成的最大 token 数。 */
    uint32_t max_tokens = 1024;
};

/**
 * @brief 聊天补全响应对象（应用层统一出参）。
 * @details
 * Service 处理完后返回的结果
 * API 层拿它拼接成 HTTP JSON 响应返回给客户端
 */
struct ChatCompletionResponse
{
    /** @brief 本次响应所属会话 ID。 */
    std::string conversation_id;
    /** @brief 助手完整回复文本。 */
    std::string reply;
    /** @brief 实际使用的模型名称。 */
    std::string model;
    /** @brief 结束原因，例如 "stop"、"length"。 */
    std::string finish_reason;
    /** @brief 响应创建时间（毫秒时间戳）。 */
    uint64_t created_at_ms = 0;
};

/**
 * @brief 持久化写入对象（供异步写入器入队使用）。
 */
struct PersistMessage
{
    /** @brief 会话标识 SID。 */
    std::string sid;
    /** @brief 会话 ID。 */
    std::string conversation_id;
    /** @brief 消息角色。 */
    std::string role;
    /** @brief 需要落库的消息内容。 */
    std::string content;
    /** @brief 消息创建时间（毫秒时间戳）。 */
    uint64_t created_at_ms = 0;
};

} // namespace common
} // namespace ai

#endif

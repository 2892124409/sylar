#ifndef __SYLAR_AI_SERVICE_CHAT_INTERFACES_H__
#define __SYLAR_AI_SERVICE_CHAT_INTERFACES_H__

#include "ai/common/ai_types.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @file chat_interfaces.h
 * @brief ChatService 依赖的读写能力抽象接口。
 *
 * 该文件通过接口解耦业务编排层与具体存储实现，便于后续替换 MySQL、
 * 引入缓存、或在测试中注入 Mock 实现。
 */

namespace ai
{
namespace service
{

/**
 * @brief 聊天历史读取接口（读路径抽象）。
 */
class ChatStore
{
  public:
    typedef std::shared_ptr<ChatStore> ptr;
    virtual ~ChatStore() {}

    /**
     * @brief 加载最近 N 条消息，用于上下文补齐。
     * @param sid 会话标识 SID。
     * @param conversation_id 会话 ID。
     * @param limit 最大加载条数。
     * @param[out] out 输出消息列表（通常为时间正序）。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    virtual bool LoadRecentMessages(const std::string& sid,
                                    const std::string& conversation_id,
                                    size_t limit,
                                    std::vector<common::ChatMessage>& out,
                                    std::string& error) = 0;

    /**
     * @brief 查询指定会话的历史消息。
     * @param sid 会话标识 SID。
     * @param conversation_id 会话 ID。
     * @param limit 最大返回条数。
     * @param[out] out 输出消息列表。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    virtual bool LoadHistory(const std::string& sid,
                             const std::string& conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage>& out,
                             std::string& error) = 0;

    /**
     * @brief 加载会话摘要记忆。
     * @param sid 会话标识 SID。
     * @param conversation_id 会话 ID。
     * @param[out] summary 摘要文本，不存在时返回空串。
     * @param[out] updated_at_ms 摘要更新时间，不存在时返回 0。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    virtual bool LoadConversationSummary(const std::string& sid,
                                         const std::string& conversation_id,
                                         std::string& summary,
                                         uint64_t& updated_at_ms,
                                         std::string& error) = 0;

    /**
     * @brief 保存会话摘要记忆。
     * @param sid 会话标识 SID。
     * @param conversation_id 会话 ID。
     * @param summary 摘要文本。
     * @param updated_at_ms 摘要更新时间（毫秒时间戳）。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    virtual bool SaveConversationSummary(const std::string& sid,
                                         const std::string& conversation_id,
                                         const std::string& summary,
                                         uint64_t updated_at_ms,
                                         std::string& error) = 0;
};

/**
 * @brief 消息落库接口（写路径抽象）。
 */
class MessageSink
{
  public:
    typedef std::shared_ptr<MessageSink> ptr;
    virtual ~MessageSink() {}

    /**
     * @brief 提交一条待持久化消息到写入通道（通常是异步队列）。
     * @param message 待持久化消息对象。
     * @param[out] error 失败原因。
     * @return true 提交成功；false 提交失败。
     */
    virtual bool Enqueue(const common::PersistMessage& message, std::string& error) = 0;
};

} // namespace service
} // namespace ai

#endif

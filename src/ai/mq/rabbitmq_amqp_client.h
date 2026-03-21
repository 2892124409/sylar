#ifndef __SYLAR_AI_MQ_RABBITMQ_AMQP_CLIENT_H__
#define __SYLAR_AI_MQ_RABBITMQ_AMQP_CLIENT_H__

#include "ai/config/ai_app_config.h"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

/**
 * @file rabbitmq_amqp_client.h
 * @brief RabbitMQ 原生 AMQP 客户端封装。
 */

namespace ai
{
namespace mq
{

/**
 * @brief 基于 librabbitmq 的轻量 AMQP 客户端。
 * @details
 * 该客户端用于：
 * 1) 声明队列；
 * 2) 发布消息；
 * 3) 批量拉取消息（当前使用 no_ack 语义）。
 */
class RabbitMqAmqpClient
{
  public:
    /** @brief 构造客户端。 */
    explicit RabbitMqAmqpClient(const config::RabbitMqSettings& settings);

    /** @brief 发布一条消息到 RabbitMQ。 */
    bool Publish(const std::string& payload, std::string& error) const;

    /** @brief 幂等声明队列（不存在则创建）。 */
    bool EnsureQueue(std::string& error) const;

    /**
     * @brief 从队列批量拉取消息。
     * @details 当前使用 `no_ack=true`，拉取成功后消息立即从队列移除。
     */
    bool Get(size_t count, std::vector<std::string>& payloads, std::string& error) const;

  private:
    struct Session;

    bool OpenSession(Session& session, std::string& error) const;
    void CloseSession(Session& session) const;

    bool DeclareQueue(Session& session, std::string& error) const;

    bool CheckRpcReply(const char* action, const Session& session, std::string& error) const;
    std::string BuildServerError(const Session& session) const;
    static std::string BytesToString(const void* data, size_t len);

  private:
    config::RabbitMqSettings m_settings;
};

} // namespace mq
} // namespace ai

#endif

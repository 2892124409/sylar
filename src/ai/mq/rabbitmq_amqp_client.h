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
 * @details
 * 本文件定义了一个“最小可用”的 AMQP 客户端抽象，供：
 * 1) 主服务生产者发布持久化消息；
 * 2) 消费者进程批量拉取消息；
 * 3) 启动阶段幂等声明队列与绑定关系。
 */

namespace ai
{
namespace mq
{

/**
 * @brief 基于 librabbitmq 的轻量 AMQP 客户端。
 * @details
 * 设计定位：
 * 1) 不暴露 AMQP 底层细节给业务层；
 * 2) 会话按“单次操作”打开与关闭，优先保证边界清晰；
 * 3) 提供统一错误字符串，便于日志定位。
 */
class RabbitMqAmqpClient
{
  public:
    /**
     * @brief 构造客户端。
     * @param settings RabbitMQ 连接与队列配置快照。
     */
    explicit RabbitMqAmqpClient(const config::RabbitMqSettings& settings);

    /**
     * @brief 发布一条消息到 RabbitMQ。
     * @param payload 要发布的原始消息体（通常是 JSON 字符串）。
     * @param[out] error 失败时返回可读错误信息。
     * @return true 发布成功；false 发布失败。
     */
    bool Publish(const std::string& payload, std::string& error) const;

    /**
     * @brief 幂等声明队列（不存在则创建）。
     * @param[out] error 失败时返回可读错误信息。
     * @return true 声明成功；false 失败。
     */
    bool EnsureQueue(std::string& error) const;

    /**
     * @brief 从队列批量拉取消息。
     * @param count 本次最多拉取条数。
     * @param[out] payloads 拉取到的消息体集合（输出前会先清空）。
     * @param[out] error 失败时返回可读错误信息。
     * @details 当前使用 `no_ack=true`，拉取成功后消息立即从队列移除。
     * @return true 拉取流程成功（即使结果为空也可能是 true）；false 拉取失败。
     */
    bool Get(size_t count, std::vector<std::string>& payloads, std::string& error) const;

  private:
    /**
     * @brief 一次 AMQP 会话上下文。
     * @details
     * - connection: AMQP 连接句柄；
     * - socket: TCP socket 句柄；
     * - channel: 当前使用的逻辑通道号。
     */
    struct Session;

    /**
     * @brief 打开 AMQP 会话（连接 + 登录 + 开通道）。
     * @param[out] session 会话输出对象。
     * @param[out] error 失败时返回可读错误信息。
     * @return true 打开成功；false 打开失败。
     */
    bool OpenSession(Session& session, std::string& error) const;

    /**
     * @brief 关闭 AMQP 会话并清理句柄。
     * @param[in,out] session 待关闭会话。
     */
    void CloseSession(Session& session) const;

    /**
     * @brief 声明队列，并在需要时绑定 exchange/routing_key。
     * @param[in,out] session 已打开的 AMQP 会话。
     * @param[out] error 失败时返回可读错误信息。
     * @return true 声明/绑定成功；false 失败。
     */
    bool DeclareQueue(Session& session, std::string& error) const;

    /**
     * @brief 校验最近一次 RPC 调用回复状态。
     * @param action 当前动作名（用于错误日志上下文）。
     * @param session 当前会话。
     * @param[out] error 失败时返回可读错误信息。
     * @return true 回复正常；false 回复异常。
     */
    bool CheckRpcReply(const char* action, const Session& session, std::string& error) const;

    /**
     * @brief 生成服务端/库级错误描述。
     * @param session 当前会话。
     * @return 可读错误字符串。
     */
    std::string BuildServerError(const Session& session) const;

    /**
     * @brief 将 AMQP bytes 缓冲区转换为 std::string。
     * @param data 缓冲区起始地址。
     * @param len 缓冲区长度。
     * @return 转换后的字符串。
     */
    static std::string BytesToString(const void* data, size_t len);

  private:
    /** @brief RabbitMQ 配置快照。 */
    config::RabbitMqSettings m_settings;
};

} // namespace mq
} // namespace ai

#endif

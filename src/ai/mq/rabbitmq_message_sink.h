#ifndef __SYLAR_AI_MQ_RABBITMQ_MESSAGE_SINK_H__
#define __SYLAR_AI_MQ_RABBITMQ_MESSAGE_SINK_H__

#include "ai/config/ai_app_config.h"
#include "ai/mq/rabbitmq_amqp_client.h"
#include "ai/service/chat_interfaces.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @file rabbitmq_message_sink.h
 * @brief MessageSink 的 RabbitMQ 写入实现。
 */

namespace ai
{
namespace mq
{

/**
 * @brief 将 PersistMessage 写入 RabbitMQ 的异步生产者。
 * @details
 * 请求线程只负责入本地队列；后台线程负责通过 RabbitMQ AMQP 协议发布消息。
 */
class RabbitMqMessageSink : public service::MessageSink
{
  public:
    typedef std::shared_ptr<RabbitMqMessageSink> ptr;

    /** @brief 构造生产者。 */
    explicit RabbitMqMessageSink(const config::MqSettings& settings);

    /** @brief 析构时自动停止后台线程。 */
    ~RabbitMqMessageSink();

    /** @brief 启动后台发布线程。 */
    bool Start(std::string& error);

    /** @brief 停止后台发布线程。 */
    void Stop();

    /** @brief 入队一条持久化消息。 */
    virtual bool Enqueue(const common::PersistMessage& message, std::string& error) override;

  private:
    void Run();
    bool PublishPersistMessage(const common::PersistMessage& message, std::string& error);

  private:
    /** @brief MQ 配置快照。 */
    config::MqSettings m_settings;
    /** @brief RabbitMQ AMQP 客户端。 */
    RabbitMqAmqpClient m_client;

    /** @brief 运行标记。 */
    bool m_running;
    /** @brief 队列锁。 */
    std::mutex m_mutex;
    /** @brief 队列条件变量。 */
    std::condition_variable m_cond;
    /** @brief 待发布消息队列。 */
    std::deque<common::PersistMessage> m_queue;
    /** @brief 后台发布线程。 */
    std::thread m_thread;
};

} // namespace mq
} // namespace ai

#endif

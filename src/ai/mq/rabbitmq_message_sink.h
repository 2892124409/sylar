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
 * @details
 * 该组件是“业务层持久化抽象（MessageSink）”到“RabbitMQ 发布”的桥接层：
 * 1) 上游（ChatService）只做 Enqueue，不直接做网络 IO；
 * 2) 下游由后台线程统一发布到 RabbitMQ；
 * 3) 通过本地队列实现请求线程与 MQ 网络抖动的解耦。
 */

namespace ai
{
namespace mq
{

/**
 * @brief 将 PersistMessage 写入 RabbitMQ 的异步生产者。
 * @details
 * 线程模型：
 * 1) 请求线程：调用 Enqueue，把消息放入本地内存队列后立即返回；
 * 2) 发布线程：Run 循环消费本地队列，调用 RabbitMqAmqpClient::Publish 发送；
 * 3) 停机阶段：Stop 通知线程退出并 join，保证资源有序回收。
 */
class RabbitMqMessageSink : public service::MessageSink
{
  public:
    typedef std::shared_ptr<RabbitMqMessageSink> ptr;

    /**
     * @brief 构造生产者。
     * @param settings MQ 相关配置快照。
     */
    explicit RabbitMqMessageSink(const config::MqSettings& settings);

    /**
     * @brief 析构时自动停止后台线程。
     * @details 采用 RAII 保证对象销毁时线程不会悬挂。
     */
    ~RabbitMqMessageSink();

    /**
     * @brief 启动后台发布线程。
     * @param[out] error 启动失败时返回错误信息。
     * @return true 启动成功或已在运行；false 启动失败。
     * @details
     * 启动时会执行：
     * 1) provider/容量配置校验；
     * 2) EnsureQueue 队列预热检查；
     * 3) 创建后台线程进入 Run 循环。
     */
    bool Start(std::string& error);

    /**
     * @brief 停止后台发布线程。
     * @details 幂等；可重复调用。
     */
    void Stop();

    /**
     * @brief 入队一条持久化消息。
     * @param message 待持久化消息。
     * @param[out] error 入队失败时返回错误信息。
     * @return true 入队成功；false 入队失败。
     * @details
     * 失败场景：
     * 1) sink 未启动；
     * 2) 本地队列已满。
     */
    virtual bool Enqueue(const common::PersistMessage& message, std::string& error) override;

  private:
    /**
     * @brief 后台发布线程主循环。
     * @details
     * - 等待本地队列消息；
     * - 取一条并发布；
     * - 失败重试并记录错误日志。
     */
    void Run();

    /**
     * @brief 将 PersistMessage 序列化并发布到 RabbitMQ。
     * @param message 待发布消息。
     * @param[out] error 失败时返回错误信息。
     * @return true 发布成功；false 发布失败。
     */
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
    /** @brief 队列条件变量（用于等待新消息或退出通知）。 */
    std::condition_variable m_cond;
    /** @brief 待发布消息队列（受 m_mutex 保护）。 */
    std::deque<common::PersistMessage> m_queue;
    /** @brief 后台发布线程。 */
    std::thread m_thread;
};

} // namespace mq
} // namespace ai

#endif

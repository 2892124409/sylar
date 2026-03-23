#include "ai/mq/rabbitmq_message_sink.h"

#include "log/logger.h"

#include <nlohmann/json.hpp>

#include <chrono>

namespace ai
{
namespace mq
{

/** @brief 系统日志器。 */
static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造函数，保存配置并初始化运行态。
 */
RabbitMqMessageSink::RabbitMqMessageSink(const config::MqSettings& settings)
    : m_settings(settings)
    , m_client(settings.rabbitmq)
    , m_running(false)
{
}

/**
 * @brief 析构函数，确保后台线程退出。
 */
RabbitMqMessageSink::~RabbitMqMessageSink()
{
    Stop();
}

/**
 * @brief 启动 MQ sink。
 * @details
 * 执行顺序：
 * 1) 幂等判断（已运行直接返回 true）；
 * 2) 配置校验（provider/queue capacity）；
 * 3) EnsureQueue 预热；
 * 4) 启动后台发布线程。
 */
bool RabbitMqMessageSink::Start(std::string& error)
{
    // Step 1: 进入临界区，保护运行态与线程对象。
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
    {
        return true;
    }

    // Step 2: provider 校验，当前仅支持 rabbitmq_amqp。
    if (m_settings.provider != "rabbitmq_amqp")
    {
        error = "unsupported mq provider: " + m_settings.provider;
        return false;
    }

    // Step 3: 生产者本地队列容量必须大于 0。
    if (m_settings.producer_queue_capacity == 0)
    {
        error = "mq producer_queue_capacity must be > 0";
        return false;
    }

    // Step 4: 启动前先确保队列存在，避免运行时首次发布失败。
    if (!m_client.EnsureQueue(error))
    {
        return false;
    }

    // Step 5: 标记运行并启动后台线程。
    m_running = true;
    m_thread = std::thread(&RabbitMqMessageSink::Run, this);
    return true;
}

/**
 * @brief 停止 MQ sink。
 * @details
 * 1) 设置 m_running=false；
 * 2) 唤醒可能阻塞在条件变量上的线程；
 * 3) join 等待线程退出。
 */
void RabbitMqMessageSink::Stop()
{
    {
        // Step 1: 修改运行态（幂等保护）。
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            return;
        }
        m_running = false;
    }

    // Step 2: 通知后台线程检查退出条件。
    m_cond.notify_all();

    // Step 3: 等待线程退出，避免悬挂线程。
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

/**
 * @brief 入队一条待发布消息。
 * @details
 * 请求线程只做内存操作，不直接触发网络 IO。
 */
bool RabbitMqMessageSink::Enqueue(const common::PersistMessage& message, std::string& error)
{
    // 入队操作与运行态、队列长度共享同一把锁。
    std::lock_guard<std::mutex> lock(m_mutex);

    // 未启动时直接返回错误，避免“消息入队但无人消费”。
    if (!m_running)
    {
        error = "rabbitmq sink is not running";
        return false;
    }

    // 背压保护：队列满时拒绝入队。
    if (m_queue.size() >= m_settings.producer_queue_capacity)
    {
        error = "rabbitmq producer queue full";
        return false;
    }

    // 入队并通知后台线程处理。
    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

/**
 * @brief 后台发布线程主循环。
 * @details
 * 运行流程：
 * 1) 等待消息或退出信号；
 * 2) 取一条消息；
 * 3) 发布到 RabbitMQ（失败重试）；
 * 4) 继续下一轮。
 */
void RabbitMqMessageSink::Run()
{
    while (true)
    {
        // 当前要处理的消息对象。
        common::PersistMessage message;
        // 标记是否成功取到消息。
        bool has_message = false;

        {
            // Step 1: 等待条件变量，直到有消息或停止。
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_queue.empty() && m_running)
            {
                m_cond.wait(lock);
            }

            // 若已停止且无待处理消息，则退出线程。
            if (m_queue.empty() && !m_running)
            {
                break;
            }

            // Step 2: 从队列头取出一条消息（FIFO）。
            message = m_queue.front();
            m_queue.pop_front();
            has_message = true;
        }

        // 理论上不会触发，防御性保护。
        if (!has_message)
        {
            continue;
        }

        // Step 3: 发布失败重试。
        std::string error;
        int retry_count = 0;
        while (true)
        {
            // 发布成功，进入下一条消息。
            if (PublishPersistMessage(message, error))
            {
                break;
            }

            ++retry_count;

            // 读取最新运行态，决定停机时是否继续重试。
            bool running = true;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                running = m_running;
            }

            BASE_LOG_ERROR(g_logger) << "publish message to rabbitmq failed, retry=" << retry_count
                                     << " error=" << error;

            // 停机阶段限制最大重试次数，避免退出流程被无限阻塞。
            if (!running && retry_count >= 3)
            {
                BASE_LOG_ERROR(g_logger) << "drop message on shutdown after retries, conversation_id="
                                         << message.conversation_id;
                break;
            }

            // 简单固定退避，避免短时间内高频失败重试。
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

/**
 * @brief 序列化 PersistMessage 并调用 AMQP 客户端发布。
 */
bool RabbitMqMessageSink::PublishPersistMessage(const common::PersistMessage& message, std::string& error)
{
    // 组装统一消息协议，供消费者解码。
    nlohmann::json payload;
    payload["schema"] = "ai.persist_message.v1";
    payload["sid"] = message.sid;
    payload["conversation_id"] = message.conversation_id;
    payload["role"] = message.role;
    payload["content"] = message.content;
    payload["created_at_ms"] = message.created_at_ms;

    // 序列化为字符串后发布到 RabbitMQ。
    return m_client.Publish(payload.dump(), error);
}

} // namespace mq
} // namespace ai

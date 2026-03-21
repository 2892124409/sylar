#include "ai/mq/rabbitmq_message_sink.h"

#include "log/logger.h"

#include <nlohmann/json.hpp>

#include <chrono>

namespace ai
{
namespace mq
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

RabbitMqMessageSink::RabbitMqMessageSink(const config::MqSettings& settings)
    : m_settings(settings)
    , m_client(settings.rabbitmq)
    , m_running(false)
{
}

RabbitMqMessageSink::~RabbitMqMessageSink()
{
    Stop();
}

bool RabbitMqMessageSink::Start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
    {
        return true;
    }

    if (m_settings.provider != "rabbitmq_amqp")
    {
        error = "unsupported mq provider: " + m_settings.provider;
        return false;
    }

    if (m_settings.producer_queue_capacity == 0)
    {
        error = "mq producer_queue_capacity must be > 0";
        return false;
    }

    if (!m_client.EnsureQueue(error))
    {
        return false;
    }

    m_running = true;
    m_thread = std::thread(&RabbitMqMessageSink::Run, this);
    return true;
}

void RabbitMqMessageSink::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            return;
        }
        m_running = false;
    }

    m_cond.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool RabbitMqMessageSink::Enqueue(const common::PersistMessage& message, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running)
    {
        error = "rabbitmq sink is not running";
        return false;
    }

    if (m_queue.size() >= m_settings.producer_queue_capacity)
    {
        error = "rabbitmq producer queue full";
        return false;
    }

    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

void RabbitMqMessageSink::Run()
{
    while (true)
    {
        common::PersistMessage message;
        bool has_message = false;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_queue.empty() && m_running)
            {
                m_cond.wait(lock);
            }

            if (m_queue.empty() && !m_running)
            {
                break;
            }

            message = m_queue.front();
            m_queue.pop_front();
            has_message = true;
        }

        if (!has_message)
        {
            continue;
        }

        std::string error;
        int retry_count = 0;
        while (true)
        {
            if (PublishPersistMessage(message, error))
            {
                break;
            }

            ++retry_count;

            bool running = true;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                running = m_running;
            }

            BASE_LOG_ERROR(g_logger) << "publish message to rabbitmq failed, retry=" << retry_count
                                     << " error=" << error;

            if (!running && retry_count >= 3)
            {
                BASE_LOG_ERROR(g_logger) << "drop message on shutdown after retries, conversation_id="
                                         << message.conversation_id;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

bool RabbitMqMessageSink::PublishPersistMessage(const common::PersistMessage& message, std::string& error)
{
    nlohmann::json payload;
    payload["schema"] = "ai.persist_message.v1";
    payload["sid"] = message.sid;
    payload["conversation_id"] = message.conversation_id;
    payload["role"] = message.role;
    payload["content"] = message.content;
    payload["created_at_ms"] = message.created_at_ms;

    return m_client.Publish(payload.dump(), error);
}

} // namespace mq
} // namespace ai

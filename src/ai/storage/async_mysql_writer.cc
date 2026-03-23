#include "ai/storage/async_mysql_writer.h"

#include "log/logger.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

AsyncMySqlWriter::AsyncMySqlWriter(const MysqlConnectionPool::ptr& pool,
                                   const config::PersistSettings& persist_settings)
    : m_pool(pool)
    , m_persist_settings(persist_settings)
    , m_persister(new ChatMessagePersister(pool))
    , m_running(false)
{
}

AsyncMySqlWriter::~AsyncMySqlWriter()
{
    Stop();
}

bool AsyncMySqlWriter::Start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
    {
        return true;
    }

    if (!m_pool)
    {
        error = "mysql pool is null";
        return false;
    }

    if (!m_persister)
    {
        error = "chat message persister is null";
        return false;
    }

    // 统一交由 ChatMessagePersister 进行幂等建表与字段补齐。
    if (!m_persister->Init(error))
    {
        return false;
    }

    m_running = true;
    m_thread = std::thread(&AsyncMySqlWriter::Run, this);
    return true;
}

void AsyncMySqlWriter::Stop()
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

bool AsyncMySqlWriter::Enqueue(const common::PersistMessage& message, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running)
    {
        error = "async mysql writer is not running";
        return false;
    }

    if (m_queue.size() >= m_persist_settings.queue_capacity)
    {
        error = "persist queue full";
        return false;
    }

    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

void AsyncMySqlWriter::Run()
{
    while (true)
    {
        std::deque<common::PersistMessage> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty() && m_running)
            {
                m_cond.wait_for(lock, std::chrono::milliseconds(m_persist_settings.flush_interval_ms));
            }

            size_t fetch_count = std::min(m_queue.size(), m_persist_settings.batch_size);
            for (size_t i = 0; i < fetch_count; ++i)
            {
                batch.push_back(m_queue.front());
                m_queue.pop_front();
            }

            if (batch.empty() && !m_running)
            {
                break;
            }
        }

        if (batch.empty())
        {
            continue;
        }

        std::string error;
        if (!FlushBatch(batch, error))
        {
            BASE_LOG_ERROR(g_logger) << "Flush async mysql batch failed: " << error;
        }
    }
}

bool AsyncMySqlWriter::FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error)
{
    if (!m_persister)
    {
        error = "chat message persister is null";
        return false;
    }

    // 将 deque 转为 vector，复用统一落库执行器。
    std::vector<common::PersistMessage> messages;
    messages.reserve(batch.size());
    for (std::deque<common::PersistMessage>::const_iterator it = batch.begin(); it != batch.end(); ++it)
    {
        messages.push_back(*it);
    }
    return m_persister->PersistBatch(messages, error);
}

} // namespace storage
} // namespace ai

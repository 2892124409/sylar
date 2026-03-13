#ifndef __SYLAR_AI_STORAGE_ASYNC_MYSQL_WRITER_H__
#define __SYLAR_AI_STORAGE_ASYNC_MYSQL_WRITER_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_interfaces.h"

#include <mysql/mysql.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace ai
{
namespace storage
{

class AsyncMySqlWriter : public service::MessageSink
{
public:
    typedef std::shared_ptr<AsyncMySqlWriter> ptr;

    AsyncMySqlWriter(const config::MysqlSettings &mysql_settings,
                     const config::PersistSettings &persist_settings);
    ~AsyncMySqlWriter();

    bool Start(std::string &error);
    void Stop();

    virtual bool Enqueue(const common::PersistMessage &message, std::string &error) override;

private:
    void Run();

    bool EnsureConnected(std::string &error);
    bool EnsureSchema(std::string &error);
    bool ExecuteSql(const std::string &sql, std::string &error);

    bool FlushBatch(std::deque<common::PersistMessage> &batch, std::string &error);
    std::string Escape(const std::string &value);

private:
    config::MysqlSettings m_mysql_settings;
    config::PersistSettings m_persist_settings;

    bool m_running;
    MYSQL *m_conn;

    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<common::PersistMessage> m_queue;

    std::thread m_thread;
};

} // namespace storage
} // namespace ai

#endif

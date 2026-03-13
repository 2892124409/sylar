#ifndef __SYLAR_AI_STORAGE_CHAT_REPOSITORY_H__
#define __SYLAR_AI_STORAGE_CHAT_REPOSITORY_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_interfaces.h"

#include <mysql/mysql.h>

#include <memory>
#include <mutex>
#include <string>

namespace ai
{
namespace storage
{

class ChatRepository : public service::ChatStore
{
public:
    typedef std::shared_ptr<ChatRepository> ptr;

    explicit ChatRepository(const config::MysqlSettings &settings);
    virtual ~ChatRepository();

    bool Init(std::string &error);

    virtual bool LoadRecentMessages(const std::string &sid,
                                    const std::string &conversation_id,
                                    size_t limit,
                                    std::vector<common::ChatMessage> &out,
                                    std::string &error) override;

    virtual bool LoadHistory(const std::string &sid,
                             const std::string &conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage> &out,
                             std::string &error) override;

private:
    bool EnsureConnected(std::string &error);
    bool EnsureSchema(std::string &error);
    bool ExecuteSql(const std::string &sql, std::string &error);
    std::string Escape(const std::string &value);

private:
    config::MysqlSettings m_settings;
    MYSQL *m_conn;
    bool m_initialized;
    std::mutex m_mutex;
};

} // namespace storage
} // namespace ai

#endif

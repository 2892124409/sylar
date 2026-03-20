#ifndef __SYLAR_AI_STORAGE_API_KEY_POOL_REPOSITORY_H__
#define __SYLAR_AI_STORAGE_API_KEY_POOL_REPOSITORY_H__

#include "ai/storage/mysql_connection_pool.h"

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace storage
{

/**
 * @brief 全局 LLM API Key 池持久化仓库。
 */
class ApiKeyPoolRepository
{
  public:
    typedef std::shared_ptr<ApiKeyPoolRepository> ptr;

    /**
     * @brief 单条 API Key 记录。
     */
    struct ApiKeyRecord
    {
        uint64_t id = 0;
        std::string provider_id;
        std::string name;
        std::string api_key;
        bool enabled = false;
        int priority = 0;
        int weight = 1;
        uint64_t cooldown_until_ms = 0;
        uint64_t fail_count = 0;
    };

  public:
    explicit ApiKeyPoolRepository(const MysqlConnectionPool::ptr& pool);
    ~ApiKeyPoolRepository();

    bool Init(std::string& error);

    bool LoadEnabledKeys(const std::string& provider_id,
                         std::vector<ApiKeyRecord>& out,
                         std::string& error);

    bool MarkKeySuccess(uint64_t key_id, uint64_t now_ms, std::string& error);

    bool MarkKeyFailure(uint64_t key_id,
                        const std::string& error_code,
                        uint64_t cooldown_until_ms,
                        uint64_t now_ms,
                        std::string& error);

  private:
    bool EnsureSchema(std::string& error);
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    MysqlConnectionPool::ptr m_pool;
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif

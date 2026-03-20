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
 * @details
 * 该仓库面向第五阶段 Provider Key 池，负责：
 * - `llm_api_keys` 表结构初始化（幂等）；
 * - 按 provider_id 读取启用 key；
 * - 回写 key 成功/失败状态（fail_count、last_error、cooldown）。
 */
class ApiKeyPoolRepository
{
  public:
    typedef std::shared_ptr<ApiKeyPoolRepository> ptr;

    /**
     * @brief 单条 API Key 记录。
     * @details
     * 该结构是 `llm_api_keys` 的运行时映射对象，供 `ProviderKeyPool`
     * 构建候选 key 快照使用。
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
    /**
     * @brief 构造仓库。
     * @param pool MySQL 连接池依赖。
     */
    explicit ApiKeyPoolRepository(const MysqlConnectionPool::ptr& pool);
    ~ApiKeyPoolRepository();

    /**
     * @brief 初始化仓库（含建表）。
     * @param[out] error 失败信息。
     * @return true 成功；false 失败。
     */
    bool Init(std::string& error);

    /**
     * @brief 加载某 provider_id 的启用 key 列表。
     * @param provider_id Provider 实例 ID。
     * @param[out] out 结果列表。
     * @param[out] error 失败信息。
     * @return true 成功；false 失败。
     */
    bool LoadEnabledKeys(const std::string& provider_id,
                         std::vector<ApiKeyRecord>& out,
                         std::string& error);

    /**
     * @brief 记录 key 成功使用。
     * @details 主要清理失败状态并清零冷却。
     */
    bool MarkKeySuccess(uint64_t key_id, uint64_t now_ms, std::string& error);

    /**
     * @brief 记录 key 失败使用。
     * @details 增加失败计数，并更新错误码与冷却截止时间。
     */
    bool MarkKeyFailure(uint64_t key_id,
                        const std::string& error_code,
                        uint64_t cooldown_until_ms,
                        uint64_t now_ms,
                        std::string& error);

  private:
    /**
     * @brief 建表（幂等）。
     */
    bool EnsureSchema(std::string& error);
    /**
     * @brief 执行单条 SQL。
     */
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);
    /**
     * @brief 对输入字符串做 SQL 转义。
     */
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    MysqlConnectionPool::ptr m_pool;
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif

#include "ai/rag/vector_store.h"

#include "ai/rag/rag_http_client.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace ai
{
namespace rag
{

namespace
{

std::string NormalizeBaseUrl(const std::string& base_url)
{
    if (base_url.empty())
    {
        return "http://127.0.0.1:6333";
    }

    if (base_url[base_url.size() - 1] == '/')
    {
        return base_url.substr(0, base_url.size() - 1);
    }
    return base_url;
}

} // namespace

QdrantVectorStore::QdrantVectorStore(const VectorStoreSettings& settings)
    : m_settings(settings)
{
    m_settings.base_url = NormalizeBaseUrl(settings.base_url);
}

std::string QdrantVectorStore::BuildCollectionUrl() const
{
    return m_settings.base_url + "/collections/" + m_settings.collection;
}

std::string QdrantVectorStore::BuildPointsUrl() const
{
    return BuildCollectionUrl() + "/points?wait=false";
}

std::string QdrantVectorStore::BuildSearchUrl() const
{
    return BuildCollectionUrl() + "/points/search";
}

bool QdrantVectorStore::EnsureCollection(size_t vector_size, std::string& error)
{
    if (vector_size == 0)
    {
        error = "qdrant collection vector_size must be > 0";
        return false;
    }

    nlohmann::json body;
    body["vectors"]["size"] = vector_size;
    body["vectors"]["distance"] = "Cosine";

    HttpRequestOptions req;
    req.method = "PUT";
    req.url = BuildCollectionUrl();
    req.headers.push_back("Content-Type: application/json");
    req.body = body.dump();
    req.request_timeout_ms = m_settings.request_timeout_ms;

    long http_status = 0;
    std::string response_body;
    if (!PerformHttpRequest(req, http_status, response_body, error))
    {
        return false;
    }

    if (http_status == 409)
    {
        return true;
    }

    if (http_status < 200 || http_status >= 300)
    {
        std::ostringstream ss;
        ss << "qdrant ensure collection http status " << http_status;
        if (!response_body.empty())
        {
            ss << ", body=" << response_body;
        }
        error = ss.str();
        return false;
    }

    return true;
}

bool QdrantVectorStore::Upsert(const std::vector<VectorPoint>& points, std::string& error)
{
    if (points.empty())
    {
        return true;
    }

    nlohmann::json body;
    body["points"] = nlohmann::json::array();
    for (size_t i = 0; i < points.size(); ++i)
    {
        nlohmann::json point;
        point["id"] = points[i].id;
        point["vector"] = points[i].vector;
        point["payload"]["sid"] = points[i].payload.sid;
        point["payload"]["conversation_id"] = points[i].payload.conversation_id;
        point["payload"]["role"] = points[i].payload.role;
        point["payload"]["content"] = points[i].payload.content;
        point["payload"]["created_at_ms"] = points[i].payload.created_at_ms;
        body["points"].push_back(point);
    }

    HttpRequestOptions req;
    req.method = "PUT";
    req.url = BuildPointsUrl();
    req.headers.push_back("Content-Type: application/json");
    req.body = body.dump();
    req.request_timeout_ms = m_settings.request_timeout_ms;

    long http_status = 0;
    std::string response_body;
    if (!PerformHttpRequest(req, http_status, response_body, error))
    {
        return false;
    }

    if (http_status < 200 || http_status >= 300)
    {
        std::ostringstream ss;
        ss << "qdrant upsert http status " << http_status;
        if (!response_body.empty())
        {
            ss << ", body=" << response_body;
        }
        error = ss.str();
        return false;
    }

    return true;
}

bool QdrantVectorStore::Search(const std::string& sid,
                               const std::vector<float>& query,
                               size_t top_k,
                               double score_threshold,
                               std::vector<SearchHit>& out,
                               std::string& error)
{
    out.clear();
    if (sid.empty() || query.empty() || top_k == 0)
    {
        return true;
    }

    nlohmann::json body;
    body["vector"] = query;
    body["limit"] = top_k;
    body["with_payload"] = true;
    body["filter"]["must"] = nlohmann::json::array();
    body["filter"]["must"].push_back(
        {
            {"key", "sid"},
            {"match", {{"value", sid}}},
        });

    if (score_threshold > 0)
    {
        body["score_threshold"] = score_threshold;
    }

    HttpRequestOptions req;
    req.method = "POST";
    req.url = BuildSearchUrl();
    req.headers.push_back("Content-Type: application/json");
    req.body = body.dump();
    req.request_timeout_ms = m_settings.request_timeout_ms;

    long http_status = 0;
    std::string response_body;
    if (!PerformHttpRequest(req, http_status, response_body, error))
    {
        return false;
    }

    if (http_status == 404)
    {
        return true;
    }

    if (http_status < 200 || http_status >= 300)
    {
        std::ostringstream ss;
        ss << "qdrant search http status " << http_status;
        if (!response_body.empty())
        {
            ss << ", body=" << response_body;
        }
        error = ss.str();
        return false;
    }

    nlohmann::json parsed = nlohmann::json::parse(response_body, nullptr, false);
    if (parsed.is_discarded())
    {
        error = "qdrant search response is not valid json";
        return false;
    }

    if (!parsed.contains("result") || !parsed["result"].is_array())
    {
        return true;
    }

    const nlohmann::json& result = parsed["result"];
    for (size_t i = 0; i < result.size(); ++i)
    {
        const nlohmann::json& item = result[i];
        if (!item.is_object())
        {
            continue;
        }

        SearchHit hit;
        if (item.contains("id") && item["id"].is_number_unsigned())
        {
            hit.id = item["id"].get<uint64_t>();
        }

        if (item.contains("score") && item["score"].is_number())
        {
            hit.score = item["score"].get<double>();
        }

        if (score_threshold > 0 && hit.score < score_threshold)
        {
            continue;
        }

        if (item.contains("payload") && item["payload"].is_object())
        {
            const nlohmann::json& payload = item["payload"];
            if (payload.contains("sid") && payload["sid"].is_string())
            {
                hit.payload.sid = payload["sid"].get<std::string>();
            }
            if (payload.contains("conversation_id") && payload["conversation_id"].is_string())
            {
                hit.payload.conversation_id = payload["conversation_id"].get<std::string>();
            }
            if (payload.contains("role") && payload["role"].is_string())
            {
                hit.payload.role = payload["role"].get<std::string>();
            }
            if (payload.contains("content") && payload["content"].is_string())
            {
                hit.payload.content = payload["content"].get<std::string>();
            }
            if (payload.contains("created_at_ms") && payload["created_at_ms"].is_number_unsigned())
            {
                hit.payload.created_at_ms = payload["created_at_ms"].get<uint64_t>();
            }
        }

        if (hit.payload.content.empty())
        {
            continue;
        }
        out.push_back(hit);
    }

    return true;
}

} // namespace rag
} // namespace ai

#include "ai/common/ai_utils.h"

#include "sylar/base/util.h"

#include <atomic>
#include <cstdlib>
#include <sstream>

namespace ai
{
namespace common
{

namespace
{

std::string ExtractSidFromSetCookie(const http::HttpResponse::ptr &response)
{
    const std::vector<std::string> &cookies = response->getSetCookies();
    for (size_t i = 0; i < cookies.size(); ++i)
    {
        const std::string &cookie = cookies[i];
        if (cookie.compare(0, 4, "SID=") != 0)
        {
            continue;
        }

        size_t end = cookie.find(';');
        if (end == std::string::npos)
        {
            end = cookie.size();
        }
        return cookie.substr(4, end - 4);
    }
    return "";
}

} // namespace

uint64_t NowMs()
{
    return sylar::GetCurrentMS();
}

std::string GenerateConversationId()
{
    static std::atomic<uint64_t> s_id(0);
    std::ostringstream ss;
    ss << "conv-" << NowMs() << "-" << s_id.fetch_add(1, std::memory_order_relaxed);
    return ss.str();
}

std::string GenerateRequestId()
{
    static std::atomic<uint64_t> s_id(0);
    std::ostringstream ss;
    ss << NowMs() << "-" << s_id.fetch_add(1, std::memory_order_relaxed);
    return ss.str();
}

std::string ExtractSid(http::HttpRequest::ptr request, http::HttpResponse::ptr response)
{
    std::string sid = request->getCookie("SID");
    if (!sid.empty())
    {
        return sid;
    }

    return ExtractSidFromSetCookie(response);
}

bool ParseJsonBody(http::HttpRequest::ptr request, nlohmann::json &out, std::string &error)
{
    if (!request)
    {
        error = "request is null";
        return false;
    }

    const std::string &body = request->getBody();
    if (body.empty())
    {
        error = "request body is empty";
        return false;
    }

    out = nlohmann::json::parse(body, nullptr, false);
    if (out.is_discarded())
    {
        error = "invalid json body";
        return false;
    }

    return true;
}

uint32_t ParseLimit(const std::string &text, uint32_t default_value, uint32_t max_value)
{
    if (text.empty())
    {
        return default_value;
    }

    char *end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0' || value == 0)
    {
        return default_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return static_cast<uint32_t>(value);
}

void WriteJson(http::HttpResponse::ptr response,
               const nlohmann::json &body,
               http::HttpStatus status,
               const std::string &reason)
{
    response->setStatus(status);
    if (!reason.empty())
    {
        response->setReason(reason);
    }
    response->setHeader("Content-Type", "application/json; charset=utf-8");
    response->setBody(body.dump());
}

void WriteJsonError(http::HttpResponse::ptr response,
                    http::HttpStatus status,
                    const std::string &message,
                    const std::string &request_id,
                    const std::string &details)
{
    nlohmann::json error;
    error["ok"] = false;
    error["code"] = static_cast<int>(status);
    error["message"] = message;
    if (!details.empty())
    {
        error["details"] = details;
    }
    if (!request_id.empty())
    {
        error["request_id"] = request_id;
    }

    WriteJson(response, error, status);
}

} // namespace common
} // namespace ai

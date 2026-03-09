#include "sylar/http/http_error.h"

#include "sylar/http/http_framework_config.h"

#include <sstream>

namespace sylar
{
    namespace http
    {

        namespace
        {
            std::string EscapeJson(const std::string &value)
            {
                std::ostringstream ss;
                for (size_t i = 0; i < value.size(); ++i)
                {
                    switch (value[i])
                    {
                    case '\\':
                        ss << "\\\\";
                        break;
                    case '"':
                        ss << "\\\"";
                        break;
                    case '\n':
                        ss << "\\n";
                        break;
                    case '\r':
                        ss << "\\r";
                        break;
                    case '\t':
                        ss << "\\t";
                        break;
                    default:
                        ss << value[i];
                        break;
                    }
                }
                return ss.str();
            }
        }

        void ApplyErrorResponse(HttpResponse::ptr response,
                                HttpStatus status,
                                const std::string &message,
                                const std::string &details)
        {
            response->setStatus(status);
            response->setKeepAlive(false);

            if (HttpFrameworkConfig::GetErrorResponseFormat() == HttpFrameworkConfig::ERROR_FORMAT_JSON)
            {
                response->setHeader("Content-Type", "application/json; charset=utf-8");
                std::ostringstream ss;
                ss << "{";
                ss << "\"code\":" << static_cast<int>(status) << ",";
                ss << "\"message\":\"" << EscapeJson(message) << "\"";
                if (!details.empty())
                {
                    ss << ",\"details\":\"" << EscapeJson(details) << "\"";
                }
                ss << "}";
                response->setBody(ss.str());
                return;
            }

            response->setHeader("Content-Type", "text/plain; charset=utf-8");
            response->setBody(details.empty() ? message : (message + "\n" + details));
        }

    } // namespace http
} // namespace sylar

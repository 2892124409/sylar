#include "http/core/http_memory_pool.h"

#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/server/http_session.h"
#include "http/session/session.h"

#include <mutex>

namespace http
{

    void EnsureHttpMemoryPoolsInitialized()
    {
        static std::once_flag s_http_memory_pool_once;
        std::call_once(s_http_memory_pool_once,
                       []()
                       {
                           sylar::HashBucket::initMemoryPool(
                               static_cast<int>(sizeof(HttpRequest)),
                               static_cast<int>(sizeof(HttpResponse)),
                               static_cast<int>(sizeof(Session)),
                               static_cast<int>(sizeof(HttpSession)));
                       });
    }

} // namespace http

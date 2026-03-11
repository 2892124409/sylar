#ifndef __SYLAR_HTTP_HTTP_ERROR_H__
#define __SYLAR_HTTP_HTTP_ERROR_H__

#include "http/core/http_response.h"

namespace sylar
{
    namespace http
    {

        void ApplyErrorResponse(HttpResponse::ptr response,
                                HttpStatus status,
                                const std::string &message,
                                const std::string &details = "");

    } // namespace http
} // namespace sylar

#endif

#ifndef __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__
#define __SYLAR_HTTP_HTTP_FRAMEWORK_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

namespace sylar
{
    namespace http
    {

        class HttpFrameworkConfig
        {
        public:
            enum ErrorResponseFormat
            {
                ERROR_FORMAT_TEXT = 0,
                ERROR_FORMAT_JSON,
            };

            static size_t GetMaxHeaderSize();
            static void SetMaxHeaderSize(size_t value);

            static size_t GetMaxBodySize();
            static void SetMaxBodySize(size_t value);

            static uint64_t GetSessionSweepIntervalMs();
            static void SetSessionSweepIntervalMs(uint64_t value);

            static uint64_t GetSSEHeartbeatIntervalMs();
            static void SetSSEHeartbeatIntervalMs(uint64_t value);

            static ErrorResponseFormat GetErrorResponseFormat();
            static void SetErrorResponseFormat(ErrorResponseFormat value);
        };

    } // namespace http
} // namespace sylar

#endif

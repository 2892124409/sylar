#include "http/core/http.h"

namespace sylar
{
    namespace http
    {

        std::string HttpMethodToString(HttpMethod method)
        {
            switch (method)
            {
            case HttpMethod::GET:
                return "GET";
            case HttpMethod::POST:
                return "POST";
            case HttpMethod::PUT:
                return "PUT";
            case HttpMethod::DELETE_:
                return "DELETE";
            case HttpMethod::HEAD:
                return "HEAD";
            case HttpMethod::OPTIONS:
                return "OPTIONS";
            case HttpMethod::PATCH:
                return "PATCH";
            default:
                return "INVALID";
            }
        }

        HttpMethod StringToHttpMethod(const std::string &method)
        {
            if (method == "GET")
            {
                return HttpMethod::GET;
            }
            if (method == "POST")
            {
                return HttpMethod::POST;
            }
            if (method == "PUT")
            {
                return HttpMethod::PUT;
            }
            if (method == "DELETE")
            {
                return HttpMethod::DELETE_;
            }
            if (method == "HEAD")
            {
                return HttpMethod::HEAD;
            }
            if (method == "OPTIONS")
            {
                return HttpMethod::OPTIONS;
            }
            if (method == "PATCH")
            {
                return HttpMethod::PATCH;
            }
            return HttpMethod::INVALID_METHOD;
        }

        std::string HttpStatusToString(HttpStatus status)
        {
            switch (status)
            {
            case HttpStatus::OK:
                return "OK";
            case HttpStatus::BAD_REQUEST:
                return "Bad Request";
            case HttpStatus::NOT_FOUND:
                return "Not Found";
            case HttpStatus::REQUEST_TIMEOUT:
                return "Request Timeout";
            case HttpStatus::INTERNAL_SERVER_ERROR:
                return "Internal Server Error";
            case HttpStatus::NOT_IMPLEMENTED:
                return "Not Implemented";
            case HttpStatus::SERVICE_UNAVAILABLE:
                return "Service Unavailable";
            default:
                return "Unknown";
            }
        }

    } // namespace http
} // namespace sylar

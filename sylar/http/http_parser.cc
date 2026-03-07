#include "sylar/http/http_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace sylar {
namespace http {
namespace {

static std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

static std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static void ParseQueryString(const std::string& query, HttpRequest::ptr request) {
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string pair = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!pair.empty()) {
            size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                request->setParam(pair, "");
            } else {
                request->setParam(pair.substr(0, eq), pair.substr(eq + 1));
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

static void ParseCookieHeader(const std::string& cookie_header, HttpRequest::ptr request) {
    size_t start = 0;
    while (start < cookie_header.size()) {
        size_t end = cookie_header.find(';', start);
        std::string item = Trim(cookie_header.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!item.empty()) {
            size_t eq = item.find('=');
            if (eq == std::string::npos) {
                request->setCookie(item, "");
            } else {
                request->setCookie(Trim(item.substr(0, eq)), Trim(item.substr(eq + 1)));
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

} // namespace

HttpRequestParser::HttpRequestParser()
    : m_error(false) {
}

void HttpRequestParser::reset() {
    m_error = false;
    m_errorMessage.clear();
}

HttpRequest::ptr HttpRequestParser::parse(const std::string& buffer, size_t& consumed) {
    consumed = 0;
    reset();

    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return HttpRequest::ptr();
    }

    std::string header_block = buffer.substr(0, header_end);
    std::istringstream header_stream(header_block);
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        m_error = true;
        m_errorMessage = "empty request line";
        return HttpRequest::ptr();
    }
    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r') {
        request_line.erase(request_line.size() - 1);
    }

    std::istringstream rl(request_line);
    std::string method_string;
    std::string target;
    std::string version;
    if (!(rl >> method_string >> target >> version)) {
        m_error = true;
        m_errorMessage = "invalid request line";
        return HttpRequest::ptr();
    }

    HttpMethod method = StringToHttpMethod(method_string);
    if (method == HttpMethod::INVALID_METHOD) {
        m_error = true;
        m_errorMessage = "unsupported http method";
        return HttpRequest::ptr();
    }
    if (version.size() != 8 || version.substr(0, 5) != "HTTP/") {
        m_error = true;
        m_errorMessage = "invalid http version";
        return HttpRequest::ptr();
    }

    HttpRequest::ptr request(new HttpRequest());
    request->setMethod(method);
    request->setVersion(static_cast<uint8_t>(version[5] - '0'), static_cast<uint8_t>(version[7] - '0'));

    size_t fragment_pos = target.find('#');
    if (fragment_pos != std::string::npos) {
        request->setFragment(target.substr(fragment_pos + 1));
        target = target.substr(0, fragment_pos);
    }
    size_t query_pos = target.find('?');
    if (query_pos != std::string::npos) {
        request->setPath(target.substr(0, query_pos));
        request->setQuery(target.substr(query_pos + 1));
        ParseQueryString(request->getQuery(), request);
    } else {
        request->setPath(target.empty() ? "/" : target);
    }

    std::string line;
    while (std::getline(header_stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        if (line.empty()) {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            m_error = true;
            m_errorMessage = "invalid header line";
            return HttpRequest::ptr();
        }
        request->setHeader(Trim(line.substr(0, colon)), Trim(line.substr(colon + 1)));
    }

    size_t content_length = 0;
    if (request->hasHeader("Content-Length")) {
        const std::string content_length_str = request->getHeader("Content-Length");
        char* end = nullptr;
        unsigned long value = std::strtoul(content_length_str.c_str(), &end, 10);
        if (!end || *end != '\0') {
            m_error = true;
            m_errorMessage = "invalid content-length";
            return HttpRequest::ptr();
        }
        content_length = static_cast<size_t>(value);
    }

    size_t total_size = header_end + 4 + content_length;
    if (buffer.size() < total_size) {
        return HttpRequest::ptr();
    }

    if (content_length > 0) {
        request->setBody(buffer.substr(header_end + 4, content_length));
    }

    std::string connection = ToLower(request->getHeader("Connection"));
    if (connection == "close") {
        request->setKeepAlive(false);
    } else if (connection == "keep-alive") {
        request->setKeepAlive(true);
    } else {
        request->setKeepAlive(request->getVersionMajor() > 1 ||
                              (request->getVersionMajor() == 1 && request->getVersionMinor() >= 1));
    }

    if (request->hasHeader("Cookie")) {
        ParseCookieHeader(request->getHeader("Cookie"), request);
    }

    consumed = total_size;
    return request;
}

} // namespace http
} // namespace sylar

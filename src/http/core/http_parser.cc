#include "http/core/http_parser.h"

#include "http/core/http_framework_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace http
{
namespace
{
/**
 * @brief 去掉字符串首尾空白字符
 * @details
 * 用于处理 header 键值、cookie 片段等字段，
 * 避免因为多余空格导致解析结果不一致。
 */
static std::string Trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

/**
 * @brief 将字符串转换为小写
 * @details
 * 主要用于大小写无关的判断，例如 Connection 头。
 */
static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return value;
}

static bool IsDigit(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

enum class RequestBodyMode
{
    NONE,
    CONTENT_LENGTH,
    CHUNKED,
};

enum class ChunkParseResult
{
    INCOMPLETE,
    SUCCESS,
    ERROR,
};

static bool ParseTransferEncodings(const std::string& header, std::vector<std::string>& encodings)
{
    size_t start = 0;
    while (start <= header.size())
    {
        size_t end = header.find(',', start);
        std::string token = ToLower(Trim(header.substr(start, end == std::string::npos ? std::string::npos : end - start)));
        if (token.empty())
        {
            return false;
        }
        encodings.push_back(token);
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return !encodings.empty();
}

static bool ResolveRequestBodyMode(const HttpRequest::ptr& request,
                                   RequestBodyMode& mode,
                                   HttpRequestParser::ErrorCode& error_code,
                                   std::string& error_message)
{
    mode = RequestBodyMode::NONE;
    if (!request->hasHeader("transfer-encoding"))
    {
        if (request->hasHeader("content-length"))
        {
            mode = RequestBodyMode::CONTENT_LENGTH;
        }
        return true;
    }

    std::vector<std::string> encodings;
    if (!ParseTransferEncodings(request->getHeader("transfer-encoding"), encodings))
    {
        error_code = HttpRequestParser::ERROR_INVALID_REQUEST;
        error_message = "invalid transfer-encoding";
        return false;
    }

    if (request->hasHeader("content-length"))
    {
        error_code = HttpRequestParser::ERROR_INVALID_REQUEST;
        error_message = "content-length with transfer-encoding is not allowed";
        return false;
    }

    if (encodings.size() == 1 && encodings[0] == "identity")
    {
        return true;
    }

    if (encodings.size() == 1 && encodings[0] == "chunked")
    {
        mode = RequestBodyMode::CHUNKED;
        return true;
    }

    error_code = HttpRequestParser::ERROR_NOT_IMPLEMENTED;
    error_message = "unsupported transfer-encoding";
    return false;
}

static bool ParseChunkSizeLine(const std::string& line, size_t& chunk_size)
{
    std::string token = line;
    size_t semicolon = token.find(';');
    if (semicolon != std::string::npos)
    {
        token = token.substr(0, semicolon);
    }
    token = Trim(token);
    if (token.empty())
    {
        return false;
    }

    size_t value = 0;
    for (size_t i = 0; i < token.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(token[i]);
        if (!std::isxdigit(c))
        {
            return false;
        }

        size_t digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<size_t>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<size_t>(c - 'a' + 10);
        }
        else
        {
            digit = static_cast<size_t>(c - 'A' + 10);
        }

        if (value > (std::numeric_limits<size_t>::max() - digit) / 16)
        {
            return false;
        }
        value = value * 16 + digit;
    }

    chunk_size = value;
    return true;
}

static ChunkParseResult ParseChunkedBody(const std::string& buffer,
                                         size_t body_offset,
                                         std::string& body,
                                         size_t& consumed,
                                         HttpRequestParser::ErrorCode& error_code,
                                         std::string& error_message)
{
    body.clear();
    consumed = 0;

    size_t position = body_offset;
    const size_t max_body_size = HttpFrameworkConfig::GetMaxBodySize();

    while (true)
    {
        size_t line_end = buffer.find("\r\n", position);
        if (line_end == std::string::npos)
        {
            return ChunkParseResult::INCOMPLETE;
        }

        size_t chunk_size = 0;
        if (!ParseChunkSizeLine(buffer.substr(position, line_end - position), chunk_size))
        {
            error_code = HttpRequestParser::ERROR_INVALID_REQUEST;
            error_message = "invalid chunk size";
            return ChunkParseResult::ERROR;
        }

        position = line_end + 2;
        if (chunk_size == 0)
        {
            while (true)
            {
                size_t trailer_end = buffer.find("\r\n", position);
                if (trailer_end == std::string::npos)
                {
                    return ChunkParseResult::INCOMPLETE;
                }
                if (trailer_end == position)
                {
                    consumed = trailer_end + 2;
                    return ChunkParseResult::SUCCESS;
                }

                std::string trailer_line = buffer.substr(position, trailer_end - position);
                if (trailer_line.find(':') == std::string::npos)
                {
                    error_code = HttpRequestParser::ERROR_INVALID_REQUEST;
                    error_message = "invalid trailer line";
                    return ChunkParseResult::ERROR;
                }
                position = trailer_end + 2;
            }
        }

        if (chunk_size > max_body_size || body.size() > max_body_size - chunk_size)
        {
            error_code = HttpRequestParser::ERROR_REQUEST_TOO_LARGE;
            error_message = "request body too large";
            return ChunkParseResult::ERROR;
        }

        if (buffer.size() < position + chunk_size + 2)
        {
            return ChunkParseResult::INCOMPLETE;
        }

        body.append(buffer, position, chunk_size);
        position += chunk_size;

        if (buffer[position] != '\r' || buffer[position + 1] != '\n')
        {
            error_code = HttpRequestParser::ERROR_INVALID_REQUEST;
            error_message = "invalid chunk terminator";
            return ChunkParseResult::ERROR;
        }
        position += 2;
    }
}

/**
 * @brief 解析 query string 到 HttpRequest::params
 * @param query 形如 a=1&b=2 的查询串（不含 ?）
 * @param request 解析结果写入目标请求对象
 * @details
 * 当前为最小可用解析器：
 * - 按 & 拆分键值对
 * - 按 = 拆分 key/value
 * - 未做 URL decode
 */
static void ParseQueryString(const std::string& query, HttpRequest::ptr request)
{
    size_t start = 0;
    while (start <= query.size())
    {
        size_t end = query.find('&', start);
        std::string pair = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!pair.empty())
        {
            size_t eq = pair.find('=');
            if (eq == std::string::npos)
            {
                request->setParam(pair, "");
            }
            else
            {
                request->setParam(pair.substr(0, eq), pair.substr(eq + 1));
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
}

/**
 * @brief 解析 Cookie 头到 HttpRequest::cookies
 * @param cookie_header 形如 "k1=v1; k2=v2" 的 Cookie 字符串
 * @param request 解析结果写入目标请求对象
 * @details
 * 当前为最小可用解析器：
 * - 按 ; 拆分 cookie 项
 * - 按 = 拆分 key/value
 * - 自动 Trim 两端空白
 */
static void ParseCookieHeader(const std::string& cookie_header, HttpRequest::ptr request)
{
    size_t start = 0;
    while (start < cookie_header.size())
    {
        size_t end = cookie_header.find(';', start);
        std::string item = Trim(cookie_header.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!item.empty())
        {
            size_t eq = item.find('=');
            if (eq == std::string::npos)
            {
                request->setCookie(item, "");
            }
            else
            {
                request->setCookie(Trim(item.substr(0, eq)), Trim(item.substr(eq + 1)));
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
}

} // namespace

HttpRequestParser::HttpRequestParser()
    : m_error(false), m_errorCode(ERROR_NONE)
{
}

void HttpRequestParser::SetMaxHeaderSize(size_t value)
{
    HttpFrameworkConfig::SetMaxHeaderSize(value);
}

size_t HttpRequestParser::GetMaxHeaderSize()
{
    return HttpFrameworkConfig::GetMaxHeaderSize();
}

void HttpRequestParser::SetMaxBodySize(size_t value)
{
    HttpFrameworkConfig::SetMaxBodySize(value);
}

size_t HttpRequestParser::GetMaxBodySize()
{
    return HttpFrameworkConfig::GetMaxBodySize();
}

void HttpRequestParser::reset()
{
    m_error = false;
    m_errorCode = ERROR_NONE;
    m_errorMessage.clear();
}

HttpRequest::ptr HttpRequestParser::parse(const std::string& buffer, size_t& consumed)
{
    // 默认先认为“这次还没有消费任何字节”。
    // 只有成功解析出完整请求后，才会把 consumed 改成真实消费量。
    consumed = 0;

    // 每次 parse 之前都重置错误状态：
    // - 如果这次成功，不应保留上一次的错误
    // - 如果这次失败，会在下面重新设置错误信息
    reset();

    // 第一步：在原始缓冲区中寻找 HTTP header 结束标记。
    // HTTP 请求头必须以 "\r\n\r\n" 结束。
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        if (buffer.size() > HttpFrameworkConfig::GetMaxHeaderSize())
        {
            m_error = true;
            m_errorCode = ERROR_REQUEST_TOO_LARGE;
            m_errorMessage = "request header too large";
        }
        // 没找到 header 结束符，说明数据还不完整（典型半包场景）。
        // 这里不是错误，只是“继续等数据”。
        return HttpRequest::ptr();
    }

    if (header_end + 4 > HttpFrameworkConfig::GetMaxHeaderSize())
    {
        m_error = true;
        m_errorCode = ERROR_REQUEST_TOO_LARGE;
        m_errorMessage = "request header too large";
        return HttpRequest::ptr();
    }

    // 截取 header 文本（不包含最后的 \r\n\r\n）。
    std::string header_block = buffer.substr(0, header_end);

    // 用字符串流逐行读取 header，先读请求行，再读各个 header 字段。
    std::istringstream header_stream(header_block);

    // 请求行示例：GET /path?x=1 HTTP/1.1
    std::string request_line;
    if (!std::getline(header_stream, request_line))
    {
        // 连请求行都读不到，视为格式错误。
        m_error = true;
        m_errorCode = ERROR_INVALID_REQUEST;
        m_errorMessage = "empty request line";
        return HttpRequest::ptr();
    }

    // std::getline 以 '\n' 结尾切行，HTTP 行尾通常是 "\r\n"。
    // 所以这里把行尾可能残留的 '\r' 去掉，便于后续统一解析。
    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
    {
        request_line.erase(request_line.size() - 1);
    }

    // 拆分请求行，按“方法 + 目标 + 版本”三段解析。
    std::istringstream rl(request_line);
    std::string method_string;
    std::string target;
    std::string version;
    if (!(rl >> method_string >> target >> version))
    {
        // 请求行字段数量不对，视为非法请求行。
        m_error = true;
        m_errorCode = ERROR_INVALID_REQUEST;
        m_errorMessage = "invalid request line";
        return HttpRequest::ptr();
    }

    // 把方法字符串映射成内部枚举（例如 "GET" -> HttpMethod::GET）。
    HttpMethod method = StringToHttpMethod(method_string);
    if (method == HttpMethod::INVALID_METHOD)
    {
        // 当前框架不支持的方法，直接报错。
        m_error = true;
        m_errorCode = ERROR_INVALID_REQUEST;
        m_errorMessage = "unsupported http method";
        return HttpRequest::ptr();
    }

    // 做最基础的版本格式校验：必须形如 HTTP/x.y。
    // 当前版本仅做最小可用检查，不做复杂兼容处理。
    if (version.size() != 8 || version.substr(0, 5) != "HTTP/" ||
        version[6] != '.' || !IsDigit(version[5]) || !IsDigit(version[7]))
    {
        m_error = true;
        m_errorCode = ERROR_INVALID_REQUEST;
        m_errorMessage = "invalid http version";
        return HttpRequest::ptr();
    }

    // 到这里，请求行合法，开始创建请求对象并填充基础字段。
    HttpRequest::ptr request = std::make_shared<HttpRequest>();
    request->setMethod(method);

    // 版本字符串位置固定：HTTP/1.1 的 '1' 在下标 5 和 7。
    // 这里直接把字符数字转为整数版本号。
    request->setVersion(static_cast<uint8_t>(version[5] - '0'), static_cast<uint8_t>(version[7] - '0'));

    // 解析 URL 片段（# 后部分）。服务端通常不依赖它，但这里保留。
    size_t fragment_pos = target.find('#');
    if (fragment_pos != std::string::npos)
    {
        // 先保存 fragment，再把 target 截断掉 fragment 段。
        request->setFragment(target.substr(fragment_pos + 1));
        target = target.substr(0, fragment_pos);
    }

    // 解析 query string（? 后部分）。
    size_t query_pos = target.find('?');
    if (query_pos != std::string::npos)
    {
        // path = ? 之前的部分
        request->setPath(target.substr(0, query_pos));
        // query = ? 之后的部分
        request->setQuery(target.substr(query_pos + 1));
        // 进一步把 query 拆成 params 键值对
        ParseQueryString(request->getQuery(), request);
    }
    else
    {
        // 没有 query，target 就是 path。
        // 空 target 兜底成“/”。
        request->setPath(target.empty() ? "/" : target);
    }

    // 第二步：逐行解析请求头。
    std::string line;
    while (std::getline(header_stream, line))
    {
        // 和请求行一样，去掉行尾可能残留的 '\r'。
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }

        // 空行直接跳过。
        if (line.empty())
        {
            continue;
        }

        // header 必须包含 ':' 分隔符，格式：Key: Value
        size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            // 头字段不合法，直接标记错误。
            m_error = true;
            m_errorCode = ERROR_INVALID_REQUEST;
            m_errorMessage = "invalid header line";
            return HttpRequest::ptr();
        }

        // 去掉 key/value 两侧空白后写入 request headers。
        request->setHeader(Trim(line.substr(0, colon)), Trim(line.substr(colon + 1)));
    }

    RequestBodyMode body_mode = RequestBodyMode::NONE;
    ErrorCode body_mode_error = ERROR_NONE;
    std::string body_mode_error_message;
    if (!ResolveRequestBodyMode(request, body_mode, body_mode_error, body_mode_error_message))
    {
        m_error = true;
        m_errorCode = body_mode_error;
        m_errorMessage = body_mode_error_message;
        return HttpRequest::ptr();
    }

    // 第三步：确定 body 传输方式，并在完整时取出请求体。
    size_t content_length = 0;
    if (body_mode == RequestBodyMode::CONTENT_LENGTH)
    {
        // 读取并解析 Content-Length 文本值。
        const std::string content_length_str = request->getHeader("content-length");
        char* end = nullptr;
        unsigned long value = std::strtoul(content_length_str.c_str(), &end, 10);
        if (!end || *end != '\0')
        {
            // 出现非数字内容，视为非法 content-length。
            m_error = true;
            m_errorCode = ERROR_INVALID_REQUEST;
            m_errorMessage = "invalid content-length";
            return HttpRequest::ptr();
        }
        content_length = static_cast<size_t>(value);
        if (content_length > HttpFrameworkConfig::GetMaxBodySize())
        {
            m_error = true;
            m_errorCode = ERROR_REQUEST_TOO_LARGE;
            m_errorMessage = "request body too large";
            return HttpRequest::ptr();
        }
    }

    // header_end 指向 "\r\n\r\n" 起始位置，所以 +4 才是 body 起始。
    // total_size = header_size + 分隔符4字节 + body长度。
    size_t total_size = header_end + 4;
    if (body_mode == RequestBodyMode::CONTENT_LENGTH)
    {
        total_size += content_length;
        if (buffer.size() < total_size)
        {
            // body 还没收全，属于半包场景，继续等数据。
            return HttpRequest::ptr();
        }

        if (content_length > 0)
        {
            request->setBody(buffer.substr(header_end + 4, content_length));
        }
    }
    else if (body_mode == RequestBodyMode::CHUNKED)
    {
        std::string body;
        ErrorCode chunk_error_code = ERROR_NONE;
        std::string chunk_error_message;
        ChunkParseResult chunk_result = ParseChunkedBody(
            buffer,
            header_end + 4,
            body,
            total_size,
            chunk_error_code,
            chunk_error_message);
        if (chunk_result == ChunkParseResult::INCOMPLETE)
        {
            return HttpRequest::ptr();
        }
        if (chunk_result == ChunkParseResult::ERROR)
        {
            m_error = true;
            m_errorCode = chunk_error_code;
            m_errorMessage = chunk_error_message;
            return HttpRequest::ptr();
        }
        request->setBody(body);
    }

    // 第四步：计算 keep-alive 语义。
    // 优先看 Connection 头；若未显式给出，则按 HTTP 版本默认规则决定。
    std::string connection = ToLower(request->getHeader("connection"));
    if (connection == "close")
    {
        // 显式要求关闭连接。
        request->setKeepAlive(false);
    }
    else if (connection == "keep-alive")
    {
        // 显式要求保持连接。
        request->setKeepAlive(true);
    }
    else
    {
        // 未显式给 Connection 时：
        // - HTTP/1.1 默认 keep-alive
        // - HTTP/1.0 默认 close
        request->setKeepAlive(request->getVersionMajor() > 1 ||
                              (request->getVersionMajor() == 1 && request->getVersionMinor() >= 1));
    }

    // 第五步：如果有 Cookie 头，拆成 cookies 键值对。
    if (request->hasHeader("cookie"))
    {
        ParseCookieHeader(request->getHeader("cookie"), request);
    }

    // 告诉调用方：本次成功消费了多少字节。
    // 调用方会把这部分从连接缓冲区删除，剩余字节留给下一条请求。
    consumed = total_size;

    // 返回解析完成的请求对象。
    return request;
}

} // namespace http

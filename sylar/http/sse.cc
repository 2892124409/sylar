#include "sylar/http/sse.h"

#include <sstream>

namespace sylar {
namespace http {

int SSEWriter::sendEvent(const std::string& data,
                         const std::string& event,
                         const std::string& id,
                         uint32_t retry) {
    std::ostringstream ss;
    if (!event.empty()) {
        ss << "event: " << event << "\n";
    }
    if (!id.empty()) {
        ss << "id: " << id << "\n";
    }
    if (retry > 0) {
        ss << "retry: " << retry << "\n";
    }
    std::istringstream data_stream(data);
    std::string line;
    while (std::getline(data_stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        ss << "data: " << line << "\n";
    }
    if (data.empty()) {
        ss << "data: \n";
    }
    ss << "\n";
    std::string payload = ss.str();
    return m_session->writeFixSize(payload.c_str(), payload.size());
}

int SSEWriter::sendComment(const std::string& comment) {
    std::string payload = ": " + comment + "\n\n";
    return m_session->writeFixSize(payload.c_str(), payload.size());
}

} // namespace http
} // namespace sylar

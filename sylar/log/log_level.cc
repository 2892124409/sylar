#include "log_level.h"
#include <algorithm>

namespace sylar {

const char* LogLevel::ToString(LogLevel::Level level) {
    switch(level) {
/* 
 * 定义一个宏 XX，用于简化 switch-case 代码
 * #name 表示将参数 name 转换为字符串常量
 * 例如 XX(DEBUG) 展开后为：
 * case LogLevel::DEBUG: return "DEBUG"; break;
 */
#define XX(name) \
    case LogLevel::name: \
        return #name; \
        break;

        XX(DEBUG);
        XX(INFO);
        XX(WARN);
        XX(ERROR);
        XX(FATAL);
#undef XX
    default:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

LogLevel::Level LogLevel::FromString(const std::string& str) {
    std::string s = str;
    // 将输入字符串转为全大写，方便比较
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);

/* 
 * 定义一个宏 XX，用于简化字符串比较代码
 * #name 将枚举名转为字符串进行比较
 * 例如 XX(DEBUG, DEBUG) 展开后为：
 * if(s == "DEBUG") { return LogLevel::DEBUG; }
 */
#define XX(level, name) \
    if(s == #name) { \
        return LogLevel::level; \
    }

    XX(DEBUG, DEBUG);
    XX(INFO, INFO);
    XX(WARN, WARN);
    XX(ERROR, ERROR);
    XX(FATAL, FATAL);
#undef XX

    return LogLevel::UNKNOWN;
}

}
#include "logger.h"
#include <iostream>

namespace sylar {

// ======================= Logger 实现 =======================

Logger::Logger(const std::string& name)
    :m_name(name)
    ,m_level(LogLevel::DEBUG) {
    /**
     * 默认格式模板说明：
     * %d{%Y-%m-%d %H:%M:%S} : 时间
     * %T : Tab制表符
     * %t : 线程id
     * %N : 线程名称
     * %F : 协程id
     * [%p] : 日志级别
     * [%c] : 日志器名称
     * %f:%l : 文件名:行号
     * %m : 消息内容
     * %n : 换行
     */
    m_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

void Logger::setFormatter(LogFormatter::ptr val) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_formatter = val;

    // 级联更新：如果下属的appender没有自己的formatter，则同步更新为logger的formatter
    for(auto& i : m_appenders) {
        if(!i->getFormatter()) {
            i->setFormatter(m_formatter);
        }
    }
}

void Logger::setFormatter(const std::string& val) {
    sylar::LogFormatter::ptr new_val(new sylar::LogFormatter(val));
    if(new_val->isError()) {
        std::cout << "Logger setFormatter name=" << m_name
                  << " value=" << val << " invalid formatter"
                  << std::endl;
        return;
    }
    setFormatter(new_val);
}

LogFormatter::ptr Logger::getFormatter() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_formatter;
}

void Logger::addAppender(LogAppender::ptr appender) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 策略：如果新加入的appender没有formatter，则继承logger的默认formatter
    if(!appender->getFormatter()) {
        appender->setFormatter(m_formatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for(auto it = m_appenders.begin();
            it != m_appenders.end(); ++it) {
        if(*it == appender) {
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::clearAppenders() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_appenders.clear();
}

void Logger::log(LogLevel::Level level, LogEvent::ptr event) {
    if(level >= m_level) {
        // 使用 shared_from_this 获取自身的 shared_ptr
        // 这是为了保证在传给 Appender 时，Logger 对象的生命周期是安全的
        auto self = shared_from_this();
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!m_appenders.empty()) {
            // 遍历所有输出地进行分发
            for(auto& i : m_appenders) {
                i->log(self, level, event);
            }
        } else if(m_root) {
            // 如果该 Logger 没有配置 Appender，则由 root 日志器兜底输出
            m_root->log(level, event);
        }
    }
}

void Logger::debug(LogEvent::ptr event) {
    log(LogLevel::DEBUG, event);
}

void Logger::info(LogEvent::ptr event) {
    log(LogLevel::INFO, event);
}

void Logger::warn(LogEvent::ptr event) {
    log(LogLevel::WARN, event);
}

void Logger::error(LogEvent::ptr event) {
    log(LogLevel::ERROR, event);
}

void Logger::fatal(LogEvent::ptr event) {
    log(LogLevel::FATAL, event);
}

// ======================= LoggerManager 实现 =======================

LoggerManager::LoggerManager() {
    // 构造时直接初始化 root 日志器
    m_root.reset(new Logger);
    // 默认输出到控制台
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));

    m_loggers[m_root->getName()] = m_root;

    init();
}

Logger::ptr LoggerManager::getLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_loggers.find(name);
    if(it != m_loggers.end()) {
        return it->second;
    }

    // 若请求的日志器不存在，则现场创建一个
    Logger::ptr logger(new Logger(name));
    // 让新日志器记住它的根，以便在没配Appender时能冒泡打印
    logger->m_root = m_root;
    m_loggers[name] = logger;
    return logger;
}

LoggerManager* LoggerManager::GetInstance() {
    // 经典的 Meyers Singleton
    static LoggerManager s_instance;
    return &s_instance;
}

void LoggerManager::init() {
    // 预留接口，未来用于从配置文件加载日志器配置
}

} // namespace sylar

#ifndef __SYLAR_LOG_LOGGER_H__
#define __SYLAR_LOG_LOGGER_H__

#include <memory>
#include <string>
#include <list>
#include <map>
#include <mutex>
#include "log_level.h"
#include "log_formatter.h"
#include "log_appender.h"
#include "../base/util.h"

namespace sylar
{

    class Logger;
    class LoggerManager;

    /**
     * @brief 日志事件包装器
     * @details 关键点：利用析构函数触发日志写入
     */
    class LogEventWrap {
    public:
        LogEventWrap(LogEvent::ptr e);
        ~LogEventWrap();
        std::stringstream& getSS();
        LogEvent::ptr getEvent() const { return m_event; }
    private:
        LogEvent::ptr m_event;
    };

    /**
     * @brief 日志器
     * @details
     * 1. 继承 enable_shared_from_this，以便在 log 方法中将自己的 shared_ptr 传给 LogEvent
     * 2. 包含多个 Appender，日志会分发给所有 Appender
     */
    class Logger : public std::enable_shared_from_this<Logger> {
        friend class LoggerManager;
    public:
        typedef std::shared_ptr<Logger> ptr;
        typedef std::mutex MutexType;

        /**
         * @brief 构造函数
         * @param[in] name 日志器名称
         */
        Logger(const std::string &name = "root");

        /**
         * @brief 写日志
         * @param[in] level 日志级别
         * @param[in] event 日志事件
         */
        void log(LogLevel::Level level, LogEvent::ptr event);

        /**
         * @brief 写debug级别日志
         * @param[in] event 日志事件
         */
        void debug(LogEvent::ptr event);

        /**
         * @brief 写info级别日志
         * @param[in] event 日志事件
         */
        void info(LogEvent::ptr event);

        /**
         * @brief 写warn级别日志
         * @param[in] event 日志事件
         */
        void warn(LogEvent::ptr event);

        /**
         * @brief 写error级别日志
         * @param[in] event 日志事件
         */
        void error(LogEvent::ptr event);

        /**
         * @brief 写fatal级别日志
         * @param[in] event 日志事件
         */
        void fatal(LogEvent::ptr event);

        /**
         * @brief 添加日志输出地
         * @param[in] appender 日志输出地
         */
        void addAppender(LogAppender::ptr appender);

        /**
         * @brief 删除日志输出地
         * @param[in] appender 日志输出地
         */
        void delAppender(LogAppender::ptr appender);

        /**
         * @brief 清空日志输出地
         */
        void clearAppenders();

        /**
         * @brief 获取日志级别
         */
        LogLevel::Level getLevel() const { return m_level; }

        /**
         * @brief 设置日志级别
         */
        void setLevel(LogLevel::Level val) { m_level = val; }

        /**
         * @brief 获取日志器名称
         */
        const std::string &getName() const { return m_name; }

        /**
         * @brief 设置日志格式器
         */
        void setFormatter(LogFormatter::ptr val);

        /**
         * @brief 设置日志格式模板
         */
        void setFormatter(const std::string &val);

        /**
         * @brief 获取日志格式器
         */
        LogFormatter::ptr getFormatter();

    private:
        std::string m_name;                      // 日志名称
        LogLevel::Level m_level;                 // 日志级别
        MutexType m_mutex;                       // 互斥锁
        std::list<LogAppender::ptr> m_appenders; // Appender集合
        LogFormatter::ptr m_formatter;           // 默认格式器
        Logger::ptr m_root;                      // 主日志器（用于兜底）
    };

    /**
     * @brief 日志器管理器
     */
    class LoggerManager
    {
    public:
        typedef std::mutex MutexType;

        /**
         * @brief 获取单例实例
         */
        static LoggerManager *GetInstance();

        /**
         * @brief 获取日志器
         * @param[in] name 日志器名称
         */
        Logger::ptr getLogger(const std::string &name);

        /**
         * @brief 初始化
         */
        void init();

        /**
         * @brief 返回主日志器
         */
        Logger::ptr getRoot() const { return m_root; }

    private:
        /**
         * @brief 构造函数设为私有
         */
        LoggerManager();

        // 禁用拷贝和赋值
        LoggerManager(const LoggerManager &) = delete;
        LoggerManager &operator=(const LoggerManager &) = delete;

        MutexType m_mutex;
        std::map<std::string, Logger::ptr> m_loggers; // 日志器容器
        Logger::ptr m_root;                           // 主日志器
    };

    /**
     * @brief 方便使用的全局管理类
     */
    typedef LoggerManager LoggerMgr;

} // namespace sylar

/**
 * @brief 使用流式方式将日志级别level的日志写入到logger
 * @details 核心点：创建临时LogEventWrap对象，行结束时触发析构执行log
 */
#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, sylar::GetThreadId(), \
                        sylar::GetFiberId(), time(0), "main"))).getSS()

#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)
#define SYLAR_LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)
#define SYLAR_LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)
#define SYLAR_LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)

/**
 * @brief 获取主日志器
 */
#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

/**
 * @brief 获取指定名称的日志器
 */
#define SYLAR_LOG_NAME(name) sylar::LoggerMgr::GetInstance()->getLogger(name)

#endif
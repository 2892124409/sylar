#ifndef __SYLAR_LOG_LOG_APPENDER_H__
#define __SYLAR_LOG_LOG_APPENDER_H__

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <fstream>
#include "log_level.h"
#include "log_formatter.h"

namespace sylar
{

    class Logger; // 前置声明

    /**
     * @brief 日志输出地（虚基类）
     */
    class LogAppender
    {
    public:
        typedef std::shared_ptr<LogAppender> ptr;

        virtual ~LogAppender() {}

        /**
         * @brief 写入日志
         * @param[in] logger 日志器
         * @param[in] level 日志级别
         * @param[in] event 日志事件
         */
        virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

        /**
         * @brief 设置格式器
         */
        void setFormatter(LogFormatter::ptr val);

        /**
         * @brief 获取格式器
         */
        LogFormatter::ptr getFormatter() const;

        LogLevel::Level getLevel() const { return m_level; }
        void setLevel(LogLevel::Level val) { m_level = val; }

    protected:
        LogLevel::Level m_level = LogLevel::DEBUG;
        LogFormatter::ptr m_formatter;
        std::mutex m_mutex; // 互斥锁，保护输出操作
    };

    /**
     * @brief 输出到控制台的Appender
     */
    class StdoutLogAppender : public LogAppender
    {
    public:
        typedef std::shared_ptr<StdoutLogAppender> ptr;
        void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    };

    /**
     * @brief 输出到文件的Appender
     */
    class FileLogAppender : public LogAppender
    {
    public:
        typedef std::shared_ptr<FileLogAppender> ptr;
        FileLogAppender(const std::string &filename);
        void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;

        /**
         * @brief 重新打开文件
         * @return 成功返回true
         */
        bool reopen();

    private:
        std::string m_filename;
        std::ofstream m_filestream; // 文件输出流
        uint64_t m_lastTime = 0;    // 上次打开时间
    };

} // namespace sylar

#endif

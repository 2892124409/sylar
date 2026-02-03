#ifndef __SYLAR_LOG_LOG_EVENT_H__
#define __SYLAR_LOG_LOG_EVENT_H__

#include <memory>
#include <string>
#include <stdint.h>
#include <sstream>
#include "log_level.h"

namespace sylar
{

    class Logger; // 前置声明

    /**
     * @brief 日志事件
     * 封装了日志发生时的现场信息
     */
    class LogEvent
    {
    public:
        // 定义智能指针类型，方便外部直接通过 LogEvent::ptr 使用，无需每次写 std::shared_ptr<LogEvent>
        typedef std::shared_ptr<LogEvent> ptr;

        /**
         * @brief 构造函数
         * @param[in] logger 日志器
         * @param[in] level 日志级别
         * @param[in] file 文件名
         * @param[in] line 行号
         * @param[in] elapse 程序启动开始到现在的毫秒数
         * @param[in] thread_id 线程id
         * @param[in] fiber_id 协程id
         * @param[in] time 日志时间(秒)
         */
        LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level, const char *file, int32_t line, uint32_t elapse, uint32_t thread_id, uint32_t fiber_id, uint64_t time) 
            : m_file(file)
            , m_line(line)
            , m_elapse(elapse)
            , m_threadId(thread_id)
            , m_fiberId(fiber_id)
            , m_time(time)
            , m_logger(logger)
            , m_level(level)
        {
        }

        const char *getFile() const { return m_file; }
        int32_t getLine() const { return m_line; }
        uint32_t getElapse() const { return m_elapse; }
        uint32_t getThreadId() const { return m_threadId; }
        uint32_t getFiberId() const { return m_fiberId; }
        uint64_t getTime() const { return m_time; }

        /**
         * @brief 获取日志内容流
         * 外部通过这个流往 event 里写入具体内容
         */
        std::stringstream &getSS() { return m_ss; }

        /**
         * @brief 获取日志内容（将流里的内容转为字符串）
         */
        std::string getContent() const { return m_ss.str(); }

        std::shared_ptr<Logger> getLogger() const { return m_logger; }
        LogLevel::Level getLevel() const { return m_level; }

    private:
        const char *m_file = nullptr; // 文件名
        int32_t m_line = 0;           // 行号
        uint32_t m_elapse = 0;        // 程序启动开始到现在的毫秒数
        uint32_t m_threadId = 0;      // 线程ID
        uint32_t m_fiberId = 0;       // 协程ID
        uint64_t m_time = 0;          // 时间戳
        std::stringstream m_ss;       // 日志内容流

        std::shared_ptr<Logger> m_logger; // 日志器
        LogLevel::Level m_level;          // 日志级别
    };

} // namespace sylar

#endif

#ifndef __SYLAR_LOG_LOG_FORMATTER_H__
#define __SYLAR_LOG_LOG_FORMATTER_H__

#include <memory>
#include <string>
#include <vector>
#include <iostream> // for std::ostream
#include "log_event.h"

namespace sylar
{

    /**
     * @brief 日志格式化器
     * @details 负责解析日志格式模板，并将日志事件格式化为字符串
     */
    class LogFormatter
    {
    public:
        typedef std::shared_ptr<LogFormatter> ptr;

        /**
         * @brief 构造函数
         * @param[in] pattern 格式模板
         * @details
         *  %m 消息
         *  %p 日志级别
         *  %r 累计毫秒数
         *  %c 日志名称
         *  %t 线程id
         *  %n 换行
         *  %d 时间
         *  %f 文件名
         *  %l 行号
         *  %T 制表符
         *  %F 协程id
         *  %N 线程名称
         *
         *  默认格式 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
         */
        LogFormatter(const std::string &pattern);

        /**
         * @brief 返回格式化后的日志文本
         */
        std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    public:
        /**
         * @brief 日志内容项格式化抽象基类
         * @details 每个FormatItem负责格式化日志中的某一部分（如时间、线程ID等）
         */
        class FormatItem
        {
        public:
            typedef std::shared_ptr<FormatItem> ptr;
            virtual ~FormatItem() {}
            /**
             * @brief 格式化日志到流
             * @param[in, out] os 输出流
             * @param[in] logger 日志器
             * @param[in] level 日志级别
             * @param[in] event 日志事件
             */
            virtual void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;
        };

        /**
         * @brief 初始化，解析模式串
         * @details 解析 m_pattern，将解析出的 FormatItem 放入 m_items
         */
        void init();

        /**
         * @brief 是否有错误
         */
        bool isError() const { return m_error; }

        /**
         * @brief 返回格式模板
         */
        const std::string getPattern() const { return m_pattern; }

    private:
        std::string m_pattern;                // 日志格式模板
        std::vector<FormatItem::ptr> m_items; // 解析后的格式子项列表，这里利用多态的性质，通过父类指针操作子类
        bool m_error = false;                 // 是否解析出错
    };

} // namespace sylar

#endif

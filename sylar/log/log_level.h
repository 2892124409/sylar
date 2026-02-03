#ifndef __SYLAR_LOG_LOG_LEVEL_H_ // 一般命名方式为全大写文件路径用_连接
#define __SYLAR_LOG_LOG_LEVEL_H__

#include <string>

namespace sylar
{
    /**
     * @brief 日志级别
     */
    class LogLevel
    {
    public:
        /**
         * @brief 日志级别枚举
         * 不用enum class的原因是：需要频繁把Level和整数比较，如果用enum class需要进行强制类型转换
         */
        enum Level
        {
            // 未知级别
            UNKNOWN = 0,
            // 调试信息：极细粒度的信息，对调试应用程序有帮助
            DEBUG = 1,
            // 一般信息：在粗粒度级别上强调应用程序的运行过程
            INFO = 2,
            // 警告信息：可能会导致错误的信息
            WARN = 3,
            // 错误信息：虽然发生错误，但依然不影响系统运行
            ERROR = 4,
            // 致命信息：严重错误，将导致应用程序停止运行
            FATAL = 5
        };

        /**
         * @brief 将日志级别转成文本输出
         * @param[in] level 日志级别
         */
        static const char *ToString(LogLevel::Level level);

        /**
         * @brief 将文本形式的日志级别转成枚举级别
         * @param[in] str 日志级别文本
         */
        static LogLevel::Level FromString(const std::string &str);
    };
}

#endif
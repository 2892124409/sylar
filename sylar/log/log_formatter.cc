#include "log_formatter.h"
#include <map>
#include <functional>
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <time.h>
#include "logger.h" // 引入logger.h，以便使用Logger的方法

namespace sylar {

// ======================= 各类格式化子项实现 =======================

/**
 * @brief 消息内容格式化器 (%m)
 */
class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getContent();
    }
};

/**
 * @brief 日志级别格式化器 (%p)
 */
class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << LogLevel::ToString(level);
    }
};

/**
 * @brief 累计毫秒数格式化器 (%r)
 */
class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << event->getElapse();
    }
};

/**
 * @brief 日志器名称格式化器 (%c)
 */
class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override {
        os << logger->getName(); 
    }
};

    /**
     * @brief 线程ID格式化器 (%t)
     */
    class ThreadIdFormatItem : public LogFormatter::FormatItem
    {
    public:
        ThreadIdFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << event->getThreadId();
        }
    };

    /**
     * @brief 协程ID格式化器 (%F)
     */
    class FiberIdFormatItem : public LogFormatter::FormatItem
    {
    public:
        FiberIdFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << event->getFiberId();
        }
    };

    /**
     * @brief 线程名称格式化器 (%N)
     */
    class ThreadNameFormatItem : public LogFormatter::FormatItem
    {
    public:
        ThreadNameFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << event->getThreadName();
        }
    };

    /**
     * @brief 时间格式化器 (%d)
     */
    class DateTimeFormatItem : public LogFormatter::FormatItem
    {
    public:
        DateTimeFormatItem(const std::string &format = "%Y-%m-%d %H:%M:%S")
            : m_format(format)
        {
            if (m_format.empty())
            {
                m_format = "%Y-%m-%d %H:%M:%S";
            }
        }

        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            struct tm tm;
            time_t time = event->getTime();
            localtime_r(&time, &tm);
            char buf[64];
            strftime(buf, sizeof(buf), m_format.c_str(), &tm);
            os << buf;
        }

    private:
        std::string m_format;
    };

    /**
     * @brief 文件名格式化器 (%f)
     */
    class FilenameFormatItem : public LogFormatter::FormatItem
    {
    public:
        FilenameFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << event->getFile();
        }
    };

    /**
     * @brief 行号格式化器 (%l)
     */
    class LineFormatItem : public LogFormatter::FormatItem
    {
    public:
        LineFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << event->getLine();
        }
    };

    /**
     * @brief 换行符格式化器 (%n)
     */
    class NewLineFormatItem : public LogFormatter::FormatItem
    {
    public:
        NewLineFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << std::endl;
        }
    };

    /**
     * @brief 固定字符串格式化器
     * 用于输出格式模板中的普通字符，如空格、中括号等
     */
    class StringFormatItem : public LogFormatter::FormatItem
    {
    public:
        StringFormatItem(const std::string &str)
            : m_string(str) {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << m_string;
        }

    private:
        std::string m_string;
    };

    /**
     * @brief 制表符格式化器 (%T)
     */
    class TabFormatItem : public LogFormatter::FormatItem
    {
    public:
        TabFormatItem(const std::string &str = "") {}
        void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
        {
            os << "\t";
        }
    };

    // ======================= LogFormatter 实现 =======================

    LogFormatter::LogFormatter(const std::string &pattern)
        : m_pattern(pattern)
    {
        init();
    }

    std::string LogFormatter::format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
    {
        std::stringstream ss;
        for (auto &i : m_items) // 如果没有&，每次循环都会拷贝一个stared_ptr；加上&表示引用不需要拷贝
        {
            i->format(ss, logger, level, event); // 使用流式拼接
        }
        return ss.str();
    }

    // Pattern解析逻辑
    void LogFormatter::init()
    {
        // 按顺序存储解析出来的pattern元素
        // pair.first: 0-普通字符串, 1-格式项
        // pair.second: 内容 (如果是0则是字符串内容，如果是1则是格式字符，如d, m, p)
        std::vector<std::pair<int, std::string>> patterns;
        std::string nstr; // 暂存普通字符串

        // 遍历模式串
        for (size_t i = 0; i < m_pattern.size(); ++i)
        {
            // 如果不是%，说明是普通字符
            if (m_pattern[i] != '%')
            {
                nstr.append(1, m_pattern[i]);
                continue;
            }

            // 如果是%%，转义为普通字符%
            if ((i + 1) < m_pattern.size())
            {
                if (m_pattern[i + 1] == '%')
                {
                    nstr.append(1, '%');
                    continue;
                }
            }

            // 走到这里说明遇到了%，开始解析格式项
            size_t n = i + 1;
            int fmt_status = 0; // 0: 解析普通格式字符, 1: 解析扩展格式(即{}中的内容)
            size_t fmt_begin = 0;

            std::string str; // 格式字符 (如 d, p)
            std::string fmt; // 扩展格式内容 (如 %Y-%m-%d)

            while (n < m_pattern.size())
            {
                // 状态0：正在解析普通格式字符（非字母且非{}时结束）
                if (!fmt_status && (!isalpha(m_pattern[n]) && m_pattern[n] != '{' && m_pattern[n] != '}'))
                {
                    str = m_pattern.substr(i + 1, n - i - 1);
                    break;
                }
                if (fmt_status == 0)
                {
                    if (m_pattern[n] == '{')
                    {
                        // 遇到{，说明前面是格式字符，后面是扩展格式
                        str = m_pattern.substr(i + 1, n - i - 1);
                        fmt_status = 1; // 进入扩展格式解析状态
                        fmt_begin = n;
                        ++n;
                        continue;
                    }
                }
                else if (fmt_status == 1)
                {
                    if (m_pattern[n] == '}')
                    {
                        // 遇到}，扩展格式解析结束
                        fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
                        fmt_status = 0;
                        ++n;
                        break;
                    }
                }
                ++n;
                // 到了字符串末尾
                if (n == m_pattern.size())
                {
                    if (str.empty())
                    {
                        str = m_pattern.substr(i + 1);
                    }
                }
            }

            if (fmt_status == 0)
            {
                // 如果之前有缓存的普通字符串，先加入列表
                if (!nstr.empty())
                {
                    patterns.push_back(std::make_pair(0, nstr));
                    nstr.clear();
                }
                // 加入格式项
                patterns.push_back(std::make_pair(1, str));
                // 如果有扩展格式，紧随其后加入（这里约定类型2为扩展格式内容，但下面的处理逻辑目前是把fmt作为参数传给构造函数）
                if (!fmt.empty())
                {
                    patterns.back().second = str;
                    patterns.push_back(std::make_pair(2, fmt));
                }
            }

            i = n - 1;
        }

        // 处理末尾剩余的普通字符串
        if (!nstr.empty())
        {
            patterns.push_back(std::make_pair(0, nstr));
        }

        // 映射表：字符 -> Item创建函数
        static std::map<std::string, std::function<FormatItem::ptr(const std::string &str)>> s_format_items = {
#define XX(str, C) \
    {#str, [](const std::string &fmt) { return FormatItem::ptr(new C(fmt)); }},

            XX(m, MessageFormatItem)
                XX(p, LevelFormatItem)
                    XX(r, ElapseFormatItem)
                        XX(c, NameFormatItem)
                            XX(t, ThreadIdFormatItem)
                                XX(n, NewLineFormatItem)
                                    XX(d, DateTimeFormatItem)
                                        XX(f, FilenameFormatItem)
                                            XX(l, LineFormatItem)
                                                XX(T, TabFormatItem)
                                                    XX(F, FiberIdFormatItem)
                                                        XX(N, ThreadNameFormatItem)
#undef XX
        };

        // ----------- 重新整理解析结果 -----------
        // 为了更方便后续处理，我们把解析出来的结果转存到 vector<tuple> 中
        // <str, format, type> type=0: 普通字符串, type=1: 格式项
        m_items.clear();
        std::string str, format;
        std::vector<std::tuple<std::string, std::string, int>> vec;

        // 这里其实重新跑了一遍上面的逻辑，只是为了生成 vec 结构，这在原版 Sylar 代码中逻辑有点冗余
        // 我们为了保持原味，保留这个结构，但你可以看到它本质上是把上面的 patterns 再清洗一遍
        nstr.clear();
        for (size_t i = 0; i < m_pattern.size(); ++i)
        {
            if (m_pattern[i] != '%')
            {
                nstr.append(1, m_pattern[i]);
                continue;
            }

            if ((i + 1) < m_pattern.size())
            {
                if (m_pattern[i + 1] == '%')
                {
                    nstr.append(1, '%');
                    continue;
                }
            }

            size_t n = i + 1;
            int fmt_status = 0;
            size_t fmt_begin = 0;

            std::string str;
            std::string fmt;

            while (n < m_pattern.size())
            {
                if (!fmt_status && (!isalpha(m_pattern[n]) && m_pattern[n] != '{' && m_pattern[n] != '}'))
                {
                    str = m_pattern.substr(i + 1, n - i - 1);
                    break;
                }
                if (fmt_status == 0)
                {
                    if (m_pattern[n] == '{')
                    {
                        str = m_pattern.substr(i + 1, n - i - 1);
                        fmt_status = 1;
                        fmt_begin = n;
                        ++n;
                        continue;
                    }
                }
                else if (fmt_status == 1)
                {
                    if (m_pattern[n] == '}')
                    {
                        fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
                        fmt_status = 0;
                        ++n;
                        break;
                    }
                }
                ++n;
                if (n == m_pattern.size())
                {
                    if (str.empty())
                    {
                        str = m_pattern.substr(i + 1);
                    }
                }
            }

            if (fmt_status == 0)
            {
                if (!nstr.empty())
                {
                    vec.push_back(std::make_tuple(nstr, std::string(), 0));
                    nstr.clear();
                }
                vec.push_back(std::make_tuple(str, fmt, 1));
                i = n - 1;
            }
            else if (fmt_status == 1)
            {
                std::cout << "pattern parse error: " << m_pattern << " - " << m_pattern.substr(i) << std::endl;
                m_error = true;
                vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));
            }
        }

        if (!nstr.empty())
        {
            vec.push_back(std::make_tuple(nstr, "", 0));
        }

        // 根据解析出的 vec，真正实例化 FormatItem
        for (auto &i : vec)
        {
            if (std::get<2>(i) == 0)
            {
                m_items.push_back(FormatItem::ptr(new StringFormatItem(std::get<0>(i))));
            }
            else
            {
                auto it = s_format_items.find(std::get<0>(i));
                if (it == s_format_items.end())
                {
                    // 如果是未知的格式字符
                    m_items.push_back(FormatItem::ptr(new StringFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
                    m_error = true;
                }
                else
                {
                    // 使用映射表中的工厂函数创建 Item
                    m_items.push_back(it->second(std::get<1>(i)));
                }
            }
        }
    }

} // namespace sylar

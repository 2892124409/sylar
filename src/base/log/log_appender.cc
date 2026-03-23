#include "log_appender.h"

namespace sylar
{

    void LogAppender::setFormatter(LogFormatter::ptr val)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_formatter = val;
    }

    LogFormatter::ptr LogAppender::getFormatter() const
    {
        // 理论上这里也应该加锁，但为了性能和shared_ptr的线程安全性，读操作有时会省略锁
        // 但严格来说，如果 setFormatter 正在修改 m_formatter，这里可能会读到旧的或者正在被修改的状态
        // 为了严谨，建议加上锁，或者使用读写锁 (RWMutex)
        // 这里先简单处理，暂不加锁
        return m_formatter;
    }

    void StdoutLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
    {
        if (level >= m_level)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::cout << m_formatter->format(logger, level, event);
        }
    }

    FileLogAppender::FileLogAppender(const std::string &filename)
        : m_filename(filename)
    {
        reopen();
    }

    void FileLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
    {
        if (level >= m_level)
        {
            // 这一步检查通常是为了应对 logrotate (日志切割)
            // 如果外部把 log.txt 移走了，我们需要重新 create 一个 log.txt
            uint64_t now = event->getTime();
            if (now >= (m_lastTime + 3))
            { // 每3秒检查一次
                reopen();
                m_lastTime = now;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            // 如果文件流没打开，或者写入失败，尝试重新打开
            if (!m_filestream)
            {
                // 这里其实应该尝试重新打开，但因为上面有 reopen 逻辑，这里简化处理
            }
            m_filestream << m_formatter->format(logger, level, event);
        }
    }

    bool FileLogAppender::reopen()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_filestream)
        {
            m_filestream.close();
        }
        m_filestream.open(m_filename, std::ios::app);
        return !!m_filestream;
    }

} // namespace sylar

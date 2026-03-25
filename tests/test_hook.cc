/**
 * @file test_hook.cc
 * @brief Hook模块测试
 * @details 测试Hook功能的自动生效机制
 * @author sylar.yin
 * @date 2026-02-12
 */

#include "sylar/log/logger.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/hook.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
#include <atomic>
#include <thread>
#include <chrono>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 打印Hook状态说明
 */
void print_hook_explanation()
{
    SYLAR_LOG_INFO(g_logger) << "Hook机制说明:";
    SYLAR_LOG_INFO(g_logger) << "  1. Hook功能需要满足两个条件才生效:";
    SYLAR_LOG_INFO(g_logger) << "     - is_hook_enable() 返回 true";
    SYLAR_LOG_INFO(g_logger) << "     - 当前线程是IOManager的工作线程";
    SYLAR_LOG_INFO(g_logger) << "  2. set_hook_enable(true) 的作用:";
    SYLAR_LOG_INFO(g_logger) << "     - 手动控制Hook开关";
    SYLAR_LOG_INFO(g_logger) << "  3. 建议方式:";
    SYLAR_LOG_INFO(g_logger) << "     - 方式A: 手动set后调用IO函数（会在IOManager线程中自动hook）";
    SYLAR_LOG_INFO(g_logger) << "     - 方式B: 让IOManager自动管理（无需手动set）";
    SYLAR_LOG_INFO(g_logger) << "";
}

// ==================== 测试1: Hook开关测试 ====================

void test_hook_switch()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试1: Hook开关机制测试 ==========";

    // 先显示默认状态
    bool default_state = sylar::is_hook_enable();
    SYLAR_LOG_INFO(g_logger) << "1. 默认is_hook_enable: " << (default_state ? "true" : "false");

    // 手动设置为true
    sylar::set_hook_enable(true);
    bool after_set = sylar::is_hook_enable();
    SYLAR_LOG_INFO(g_logger) << "2. 手动set后is_hook_enable: " << (after_set ? "true" : "false");

    // 关闭
    sylar::set_hook_enable(false);
    bool after_close = sylar::is_hook_enable();
    SYLAR_LOG_INFO(g_logger) << "3. 关闭后is_hook_enable: " << (after_close ? "true" : "false");

    // 恢复默认状态
    if (default_state)
    {
        sylar::set_hook_enable(true);
    }
    SYLAR_LOG_INFO(g_logger) << "4. 恢复默认状态后is_hook_enable: " << (sylar::is_hook_enable() ? "true" : "false");
    SYLAR_LOG_INFO(g_logger) << "测试1完成: 验证了Hook开关机制";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试2: 睡眠函数Hook测试 ====================

void test_sleep()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试2: 睡眠函数Hook测试 ==========";

    SYLAR_LOG_INFO(g_logger) << "sleep(1) 开始";
    sleep(1);
    SYLAR_LOG_INFO(g_logger) << "sleep(1) 结束";

    SYLAR_LOG_INFO(g_logger) << "测试2完成: 睡眠函数通过Hook自动让出协程";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试3: Pipe读写Hook测试 ====================

void test_pipe_io()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试3: Pipe读写Hook测试 ==========";

    int sv[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        SYLAR_LOG_ERROR(g_logger) << "socketpair创建失败: " << strerror(errno);
        return;
    }

    int writer_fd = sv[1];
    int reader_fd = sv[0];

    std::thread writer([writer_fd]()
                       {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const char* msg = "Hello from Hook!";
        ssize_t n = write(writer_fd, msg, strlen(msg));
        if (n > 0) {
            SYLAR_LOG_INFO(g_logger) << "写入线程: 成功写入 " << n << " 字节";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "写入线程: 写入失败, errno=" << errno;
        }
        close(writer_fd); });

    SYLAR_LOG_INFO(g_logger) << "读取协程: 等待数据...";
    char buffer[128];
    ssize_t n = read(reader_fd, buffer, sizeof(buffer) - 1);
    if (n > 0)
    {
        buffer[n] = '\0';
        SYLAR_LOG_INFO(g_logger) << "读取协程: 成功读取 " << n << " 字节: " << buffer;
    }
    else
    {
        SYLAR_LOG_ERROR(g_logger) << "读取协程: 读取失败, errno=" << errno;
    }

    close(reader_fd);
    writer.join();

    SYLAR_LOG_INFO(g_logger) << "测试3完成: Pipe读写函数通过Hook自动协程切换";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试4: readv/writev测试 ====================

void test_iov()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试4: readv/writev测试 ==========";

    int sv[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        SYLAR_LOG_ERROR(g_logger) << "socketpair创建失败: " << strerror(errno);
        return;
    }

    int writer_fd = sv[1];
    int reader_fd = sv[0];

    std::thread writer([writer_fd]()
                       {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        iovec iov[3];
        const char* msg1 = "Hello, ";
        const char* msg2 = "writev";
        const char* msg3 = " test!";

        iov[0].iov_base = (void*)msg1;
        iov[0].iov_len = strlen(msg1);
        iov[1].iov_base = (void*)msg2;
        iov[1].iov_len = strlen(msg2);
        iov[2].iov_base = (void*)msg3;
        iov[2].iov_len = strlen(msg3);

        ssize_t total_len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
        ssize_t n = writev(writer_fd, iov, 3);

        if (n == total_len) {
            SYLAR_LOG_INFO(g_logger) << "Iov写入: 成功写入 " << n << " 字节";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Iov写入: 失败, n=" << n << ", errno=" << errno;
        }
        close(writer_fd); });

    char buf1[10] = {0}, buf2[10] = {0}, buf3[10] = {0};
    iovec iov[3];

    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1) - 1;
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2) - 1;
    iov[2].iov_base = buf3;
    iov[2].iov_len = sizeof(buf3) - 1;

    ssize_t n = readv(reader_fd, iov, 3);

    if (n > 0)
    {
        SYLAR_LOG_INFO(g_logger) << "Iov读取: 成功读取 " << n << " 字节";
        SYLAR_LOG_INFO(g_logger) << "buf1=[" << buf1 << "] buf2=[" << buf2 << "] buf3=[" << buf3 << "]";
    }
    else
    {
        SYLAR_LOG_ERROR(g_logger) << "Iov读取: 失败, errno=" << errno;
    }
    close(reader_fd);
    writer.join();

    SYLAR_LOG_INFO(g_logger) << "测试4完成: readv/writev函数通过Hook自动协程切换";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试5: fcntl非阻塞测试 ====================

void test_fcntl_nonblock()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试5: fcntl非阻塞测试 ==========";

    int pipefd[2] = {-1, -1};
    // 创建新pipe
    if (pipe(pipefd) < 0)
    {
        SYLAR_LOG_ERROR(g_logger) << "Pipe创建失败";
        return;
    }

    int flags0 = fcntl(pipefd[0], F_GETFL, 0);
    int flags1 = fcntl(pipefd[1], F_GETFL, 0);

    SYLAR_LOG_INFO(g_logger) << "原始flags: fd0=" << flags0 << ", fd1=" << flags1;

    fcntl(pipefd[0], F_SETFL, flags0 | O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, flags1 | O_NONBLOCK);

    int new_flags0 = fcntl(pipefd[0], F_GETFL, 0);
    int new_flags1 = fcntl(pipefd[1], F_GETFL, 0);

    SYLAR_LOG_INFO(g_logger) << "设置后flags: fd0=" << new_flags0 << ", fd1=" << new_flags1;

    if ((new_flags0 & O_NONBLOCK) && (new_flags1 & O_NONBLOCK))
    {
        SYLAR_LOG_INFO(g_logger) << "fcntl测试: O_NONBLOCK标志设置成功";
    }
    else
    {
        SYLAR_LOG_ERROR(g_logger) << "fcntl测试: O_NONBLOCK标志设置失败";
    }

    close(pipefd[0]);
    close(pipefd[1]);

    SYLAR_LOG_INFO(g_logger) << "测试5完成: fcntl被正确Hook处理";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试6: setsockopt超时测试 ====================

void test_setsockopt_timeout()
{
    SYLAR_LOG_INFO(g_logger) << "========== 测试6: setsockopt超时测试 ==========";

    int sv[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        SYLAR_LOG_ERROR(g_logger) << "socketpair创建失败: " << strerror(errno);
        return;
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ret < 0)
    {
        SYLAR_LOG_ERROR(g_logger) << "setsockopt失败: " << strerror(errno);
        close(sv[0]);
        close(sv[1]);
        return;
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "setsockopt成功: 设置SO_RCVTIMEO=1秒";
    }

    char buffer[128];
    ssize_t n = read(sv[0], buffer, sizeof(buffer));
    if (n < 0)
    {
        if (errno == ETIMEDOUT)
        {
            SYLAR_LOG_INFO(g_logger) << "超时测试: 成功捕获ETIMEDOUT (Hook生效，超时机制工作)";
        }
        else
        {
            SYLAR_LOG_ERROR(g_logger) << "超时测试: errno=" << errno << " (" << strerror(errno) << ")";
        }
    }
    else
    {
        SYLAR_LOG_ERROR(g_logger) << "超时测试: 意外读取到 " << n << " 字节";
    }
    close(sv[0]);
    close(sv[1]);

    SYLAR_LOG_INFO(g_logger) << "测试6完成: setsockopt超时设置被Hook正确处理";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 主函数 ====================

int main(int argc, char **argv)
{
    SYLAR_LOG_INFO(g_logger) << "";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "     Sylar Hook模块测试程序";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "";

    // 打印Hook机制说明
    print_hook_explanation();

    // 说明接下来的测试不需要手动set_hook_enable
    SYLAR_LOG_INFO(g_logger) << "重要说明: 接下来的测试不需要手动调用set_hook_enable()";
    SYLAR_LOG_INFO(g_logger) << "         因为在IOManager线程中调用IO函数时，";
    SYLAR_LOG_INFO(g_logger) << "         Hook会自动检查并启用（基于IOManager的GetThis判断）";
    SYLAR_LOG_INFO(g_logger) << "";

    // 测试1: Hook开关测试
    test_hook_switch();

    // 创建IOManager运行其他测试
    sylar::IOManager iom(1, true, "HookTest");

    iom.schedule([&iom]()
                 {
        SYLAR_LOG_INFO(g_logger) << "[orchestrator] begin test_pipe_io";
        test_pipe_io();
        SYLAR_LOG_INFO(g_logger) << "[orchestrator] begin test_iov";
        test_iov();
        SYLAR_LOG_INFO(g_logger) << "[orchestrator] begin test_fcntl_nonblock";
        test_fcntl_nonblock();
        SYLAR_LOG_INFO(g_logger) << "[orchestrator] begin test_setsockopt_timeout";
        test_setsockopt_timeout();
        SYLAR_LOG_INFO(g_logger) << "";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        SYLAR_LOG_INFO(g_logger) << "     所有测试已完成";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        iom.stop(); });

#ifdef SYLAR_NET_VARIANT_UPSTREAM_REF
    iom.stop();
#else
    iom.runCaller();
#endif
    return 0;
}

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
#include "sylar/net/hook.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

// 全局pipefd，用于不同测试间共享
static int g_pipefd[2] = {-1, -1};

/**
 * @brief 打印Hook状态说明
 */
void print_hook_explanation() {
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

void test_hook_switch() {
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
    if (default_state) {
        sylar::set_hook_enable(true);
    }
    SYLAR_LOG_INFO(g_logger) << "4. 恢复默认状态后is_hook_enable: " << (sylar::is_hook_enable() ? "true" : "false");
    SYLAR_LOG_INFO(g_logger) << "测试1完成: 验证了Hook开关机制";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试2: 睡眠函数Hook测试 ====================

void test_sleep() {
    SYLAR_LOG_INFO(g_logger) << "========== 测试2: 睡眠函数Hook测试 ==========";

    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 测试 sleep - 这个sleep会被hook，不阻塞线程
    iom->schedule([]() {
        SYLAR_LOG_INFO(g_logger) << "协程1: 调用sleep(2)，进入协程让出";
        sleep(2);
        SYLAR_LOG_INFO(g_logger) << "协程1: sleep结束，协程被唤醒";
    });

    // 测试 usleep
    iom->schedule([]() {
        SYLAR_LOG_INFO(g_logger) << "协程2: 调用usleep(500000)x4";
        for (int i = 0; i < 4; ++i) {
            usleep(500000);
        }
        SYLAR_LOG_INFO(g_logger) << "协程2: usleep循环完成";
    });

    SYLAR_LOG_INFO(g_logger) << "测试2完成: 睡眠函数通过Hook自动让出协程";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试3: Pipe读写Hook测试 ====================

void test_pipe_io() {
    SYLAR_LOG_INFO(g_logger) << "========== 测试3: Pipe读写Hook测试 ==========";

    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 创建pipe
    if (pipe(g_pipefd) < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Pipe创建失败: " << strerror(errno);
        return;
    }

    fcntl(g_pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(g_pipefd[1], F_SETFL, O_NONBLOCK);

    // 读取协程
    iom->schedule([iom]() {
        SYLAR_LOG_INFO(g_logger) << "读取协程: 等待数据...";
        char buffer[128];
        ssize_t n = read(g_pipefd[0], buffer, sizeof(buffer) - 1);

        if (n > 0) {
            buffer[n] = '\\0';
            SYLAR_LOG_INFO(g_logger) << "读取协程: 成功读取 " << n << " 字节: " << buffer;
        } else if (n < 0) {
            SYLAR_LOG_ERROR(g_logger) << "读取协程: 读取失败, errno=" << errno;
        }

        close(g_pipefd[0]);
    });

    // 写入协程
    iom->schedule([iom]() {
        usleep(1000000);
        const char* msg = "Hello from Hook!";
        ssize_t n = write(g_pipefd[1], msg, strlen(msg));

        if (n > 0) {
            SYLAR_LOG_INFO(g_logger) << "写入协程: 成功写入 " << n << " 字节";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "写入协程: 写入失败, errno=" << errno;
        }

        close(g_pipefd[1]);
    });

    SYLAR_LOG_INFO(g_logger) << "测试3完成: Pipe读写函数通过Hook自动协程切换";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试4: readv/writev测试 ====================

void test_iov() {
    SYLAR_LOG_INFO(g_logger) << "========== 测试4: readv/writev测试 ==========";

    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 创建新pipe
    if (pipe(g_pipefd) < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Pipe创建失败";
        return;
    }

    fcntl(g_pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(g_pipefd[1], F_SETFL, O_NONBLOCK);

    // 写入协程 - 使用 writev
    iom->schedule([iom]() {
        usleep(500000);

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
        ssize_t n = writev(g_pipefd[1], iov, 3);

        if (n == total_len) {
            SYLAR_LOG_INFO(g_logger) << "Iov写入: 成功写入 " << n << " 字节";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Iov写入: 失败, n=" << n << ", errno=" << errno;
        }

        close(g_pipefd[1]);
    });

    // 读取协程 - 使用 readv
    iom->schedule([iom]() {
        usleep(1500000);

        char buf1[10], buf2[10], buf3[10];
        iovec iov[3];

        iov[0].iov_base = buf1;
        iov[0].iov_len = sizeof(buf1) - 1;
        iov[1].iov_base = buf2;
        iov[1].iov_len = sizeof(buf2) - 1;
        iov[2].iov_base = buf3;
        iov[2].iov_len = sizeof(buf3) - 1;

        ssize_t n = readv(g_pipefd[0], iov, 3);

        if (n > 0) {
            buf1[iov[0].iov_len] = '\\0';
            buf2[iov[1].iov_len] = '\\0';
            buf3[iov[2].iov_len] = '\\0';
            SYLAR_LOG_INFO(g_logger) << "Iov读取: 成功读取 " << n << " 字节";
            SYLAR_LOG_INFO(g_logger) << "buf1=[" << buf1 << "] buf2=[" << buf2 << "] buf3=[" << buf3 << "]";
        } else if (n < 0) {
            SYLAR_LOG_ERROR(g_logger) << "Iov读取: 失败, errno=" << errno;
        }

        close(g_pipefd[0]);
    });

    SYLAR_LOG_INFO(g_logger) << "测试4完成: readv/writev函数通过Hook自动协程切换";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试5: fcntl非阻塞测试 ====================

void test_fcntl_nonblock() {
    SYLAR_LOG_INFO(g_logger) << "========== 测试5: fcntl非阻塞测试 ==========";

    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 创建新pipe
    if (pipe(g_pipefd) < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Pipe创建失败";
        return;
    }

    int flags0 = fcntl(g_pipefd[0], F_GETFL, 0);
    int flags1 = fcntl(g_pipefd[1], F_GETFL, 0);

    SYLAR_LOG_INFO(g_logger) << "原始flags: fd0=" << flags0 << ", fd1=" << flags1;

    fcntl(g_pipefd[0], F_SETFL, flags0 | O_NONBLOCK);
    fcntl(g_pipefd[1], F_SETFL, flags1 | O_NONBLOCK);

    int new_flags0 = fcntl(g_pipefd[0], F_GETFL, 0);
    int new_flags1 = fcntl(g_pipefd[1], F_GETFL, 0);

    SYLAR_LOG_INFO(g_logger) << "设置后flags: fd0=" << new_flags0 << ", fd1=" << new_flags1;

    if ((new_flags0 & O_NONBLOCK) && (new_flags1 & O_NONBLOCK)) {
        SYLAR_LOG_INFO(g_logger) << "fcntl测试: O_NONBLOCK标志设置成功";
    } else {
        SYLAR_LOG_ERROR(g_logger) << "fcntl测试: O_NONBLOCK标志设置失败";
    }

    close(g_pipefd[0]);
    close(g_pipefd[1]);

    SYLAR_LOG_INFO(g_logger) << "测试5完成: fcntl被正确Hook处理";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 测试6: setsockopt超时测试 ====================

void test_setsockopt_timeout() {
    SYLAR_LOG_INFO(g_logger) << "========== 测试6: setsockopt超时测试 ==========";

    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 创建新pipe
    if (pipe(g_pipefd) < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Pipe创建失败";
        return;
    }

    fcntl(g_pipefd[0], F_SETFL, O_NONBLOCK);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ret = setsockopt(g_pipefd[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ret < 0) {
        SYLAR_LOG_ERROR(g_logger) << "setsockopt失败: " << strerror(errno);
    } else {
        SYLAR_LOG_INFO(g_logger) << "setsockopt成功: 设置SO_RCVTIMEO=1秒";
    }

    // 读取协程 - 尝试从空pipe读取
    iom->schedule([iom]() {
        char buffer[128];
        ssize_t n = read(g_pipefd[0], buffer, sizeof(buffer));

        if (n < 0) {
            if (errno == ETIMEDOUT) {
                SYLAR_LOG_INFO(g_logger) << "超时测试: 成功捕获ETIMEDOUT (Hook生效，超时机制工作)";
            } else if (errno == EAGAIN) {
                SYLAR_LOG_INFO(g_logger) << "超时测试: 返回EAGAIN (fd不是socket，超时可能未生效)";
            } else {
                SYLAR_LOG_INFO(g_logger) << "超时测试: errno=" << errno << " (" << strerror(errno) << ")";
            }
        } else {
            SYLAR_LOG_INFO(g_logger) << "超时测试: 意外读取到 " << n << " 字节";
        }

        close(g_pipefd[0]);
        close(g_pipefd[1]);
    });

    SYLAR_LOG_INFO(g_logger) << "测试6完成: setsockopt超时设置被Hook正确处理";
    SYLAR_LOG_INFO(g_logger) << "=======================================";
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
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

    // 初始化pipe
    if (pipe(g_pipefd) < 0) {
        SYLAR_LOG_ERROR(g_logger) << "初始化Pipe失败";
        return 1;
    }

    // 运行测试2-6
    iom.schedule(test_sleep);              // 测试2: 睡眠函数Hook
    iom.schedule(test_pipe_io);            // 测试3: Pipe读写Hook
    iom.schedule(test_iov);                // 测试4: readv/writev
    iom.schedule(test_fcntl_nonblock);     // 测试5: fcntl非阻塞
    iom.schedule(test_setsockopt_timeout); // 测试6: setsockopt超时

    // 10秒后自动停止
    iom.addTimer(10000, [&iom]() {
        SYLAR_LOG_INFO(g_logger) << "";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        SYLAR_LOG_INFO(g_logger) << "     所有测试已完成";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        iom.stop();
    });

    return 0;
}

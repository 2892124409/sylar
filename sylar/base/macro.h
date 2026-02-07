/**
 * @file macro.h
 * @brief 常用宏定义
 */
#ifndef __SYLAR_BASE_MACRO_H__
#define __SYLAR_BASE_MACRO_H__

#include <string.h>
#include <assert.h>
#include "util.h"
#include "sylar/log/logger.h"

// 获取当前日志器的宏
#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

#if defined __GNUC__ || defined __llvm__
/// LIKELY 宏提示编译器：分支很有可能成立
#define SYLAR_LIKELY(x)       __builtin_expect(!!(x), 1)
/// UNLIKELY 宏提示编译器：分支很有可能不成立
#define SYLAR_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#else
#define SYLAR_LIKELY(x)      (x)
#define SYLAR_UNLIKELY(x)    (x)
#endif

/// 断言宏
#define SYLAR_ASSERT(x) \
    if(SYLAR_UNLIKELY(!(x))) { \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x \
            << "\nbacktrace:\n" \
            << sylar::BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

/// 带消息的断言宏
#define SYLAR_ASSERT2(x, w) \
    if(SYLAR_UNLIKELY(!(x))) { \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x \
            << "\n" << w \
            << "\nbacktrace:\n" \
            << sylar::BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

#endif
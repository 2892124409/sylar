#include "util.h"
#include <execinfo.h> // backtrace, backtrace_symbols
#include <sstream>
#include <cxxabi.h>   // abi::__cxa_demangle
#include <iostream>

namespace sylar {

pid_t GetThreadId() {
    return syscall(SYS_gettid);
}

uint32_t GetFiberId() {
    // 后面实现协程后再修改
    return 0;
}

/**
 * @brief 获取函数调用堆栈
 * @param[out] bt 保存调用堆栈的字符串数组
 * @param[in] size 最多回溯多少层
 * @param[in] skip 跳过最开始的多少层（通常跳过 Backtrace 函数本身）
 */
void Backtrace(std::vector<std::string>& bt, int size, int skip) {
    // 1. 分配一个指针数组，用来存储每一层堆栈的地址
    void** array = (void**)malloc((sizeof(void*) * size));
    
    // 2. 调用系统函数 backtrace 获取当前堆栈
    // s 返回实际获取到的层数
    size_t s = backtrace(array, size);

    // 3. 将地址转换成符号字符串
    // strings 是一个指向字符串数组的指针，每个字符串包含：可执行文件名、函数名（可能被修饰过）、偏移量、地址
    // 注意：backtrace_symbols 内部会 malloc 内存，需要 free(strings)
    char** strings = backtrace_symbols(array, s);
    if(strings == NULL) {
        std::cout << "backtrace_symbols error" << std::endl;
        free(array);
        return;
    }

    // 4. 遍历每一层堆栈
    for(size_t i = skip; i < s; ++i) {
        // strings[i] 的典型格式：
        // ./bin/test_log(_ZN5sylar11BacktraceERSt6vectorISsSaISsEEii+0x2b) [0x401234]
        // 我们主要想提取括号里的 _ZN... 这一串，这是被编译器修饰过(Mangled)的函数名
        
        std::string str(strings[i]);
        size_t left = str.find('(');
        size_t plus = str.find('+');
        
        // 尝试解析并还原函数名
        if (left != std::string::npos && plus != std::string::npos && left < plus) {
            std::string mangled_name = str.substr(left + 1, plus - left - 1);
            int status = 0;
            
            // 使用 abi::__cxa_demangle 将修饰过的名字还原为人类可读的 C++ 函数名
            // 比如将 _ZN5sylar11Backtrace... 还原为 sylar::Backtrace(...)
            char* demangled = abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);
            
            if (status == 0) {
                // 如果还原成功，拼接成清晰的格式
                bt.push_back(str.substr(0, left + 1) + demangled + str.substr(plus));
                free(demangled); // demangled 也是 malloc 出来的，需要释放
            } else {
                // 还原失败（可能是 C 函数或者没有符号信息），直接使用原始字符串
                bt.push_back(str);
            }
        } else {
            // 格式不符合预期，直接使用原始字符串
            bt.push_back(str);
        }
    }

    // 5. 释放内存
    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string& prefix) {
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for(size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

}

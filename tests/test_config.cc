#include "sylar/base/config.h"
#include "sylar/log/logger.h"
#include <iostream>

// 定义一个 int 类型的配置项，默认值 8080
sylar::ConfigVar<int>::ptr g_int_value_config =
    sylar::Config::Lookup("system.port", (int)8080, "system port");

// 定义一个 float 类型的配置项，默认值 10.2f
// 注意：名字不区分大小写，我们在 ConfigVarBase 构造函数里转小写了，但Lookup时最好直接用小写
sylar::ConfigVar<float>::ptr g_float_value_config =
    sylar::Config::Lookup("system.value", (float)10.2f, "system value");

int main(int argc, char** argv) {
    sylar::Logger::ptr logger = SYLAR_LOG_ROOT();

    SYLAR_LOG_INFO(logger) << "before: " << g_int_value_config->getValue();
    SYLAR_LOG_INFO(logger) << "before: " << g_float_value_config->toString();

#define XX(g_var, name, prefix) \
    { \
        const auto& v = g_var->getValue(); \
        SYLAR_LOG_INFO(logger) << #prefix " " #name " = " << v; \
        SYLAR_LOG_INFO(logger) << #prefix " " #name " yaml: " << g_var->toString(); \
    }

    XX(g_int_value_config, int_value, before);
    XX(g_float_value_config, float_value, before);

    return 0;
}

#include "sylar/base/config.h"
#include "sylar/log/logger.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

// 定义基础类型
sylar::ConfigVar<int>::ptr g_int_value_config =
    sylar::Config::Lookup("system.port", (int)8080, "system port");

// 定义复杂类型 vector
sylar::ConfigVar<std::vector<int>>::ptr g_int_vec_value_config =
    sylar::Config::Lookup("system.int_vec", std::vector<int>{1, 2}, "system int vec");

// 定义复杂类型 map
sylar::ConfigVar<std::map<std::string, int>>::ptr g_int_map_value_config =
    sylar::Config::Lookup("system.int_map", std::map<std::string, int>{{"k", 2}}, "system int map");

void test_yaml() {
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "--- Before LoadFromYaml ---";
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "int_vec: " << g_int_vec_value_config->toString();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "int_map: " << g_int_map_value_config->toString();

    // 模拟一个 YAML 配置文件内容
    YAML::Node root = YAML::Load("system:\n"
                                 "    port: 9090\n"
                                 "    int_vec: [10, 20, 30]\n"
                                 "    int_map: {\"k1\": 100, \"k2\": 200}");

    // 执行整体加载
    sylar::Config::LoadFromYaml(root);

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "--- After LoadFromYaml ---";
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "port: " << g_int_value_config->getValue();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "int_vec: " << g_int_vec_value_config->toString();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "int_map: " << g_int_map_value_config->toString();
}

int main(int argc, char** argv) {
    // 监听器测试
    g_int_value_config->addListener([](const int& old_value, const int& new_value){
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "port changed from " << old_value << " to " << new_value;
    });

    test_yaml();

    return 0;
}
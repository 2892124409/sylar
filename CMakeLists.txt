cmake_minimum_required(VERSION 3.0)

project(sylar)

set(CMAKE_CXX_STANDARD 11)

# 编译选项：
# -O0: 关闭优化（方便调试）
# -g -ggdb: 生成调试信息
# -Wall: 开启大部分警告
# -rdynamic: 让链接器把所有符号都加入到动态符号表中（这对后续打印堆栈很重要）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0 -g -ggdb -Wall -rdynamic -fPIC")

# 包含当前目录，这样引用头文件可以直接 #include "sylar/xxx.h"
include_directories(.)

# 定义一个库文件，后续源文件添加到这里
# 目前还没有源文件，先留空，等会添加 log.cc
# add_library(sylar SHARED sylar/log.cc)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

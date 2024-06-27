cmake_minimum_required(VERSION 3.29)
project(udp_forward)

# 设置源文件
set(SOURCE_FILES main.cpp)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 查找Winsock库
find_library(WS2_32_LIBRARY ws2_32)

# 链接Winsock库
target_link_libraries(${PROJECT_NAME} ${WS2_32_LIBRARY})

cmake_minimum_required(VERSION 3.29)
project(udp_forward)

# 查找Winsock库
find_library(WS2_32_LIBRARY ws2_32)

# 设置源文件
set(SERVER_FILES server.cpp)

# 添加可执行文件
add_executable(server ${SERVER_FILES})

# 链接Winsock库
target_link_libraries(server ${WS2_32_LIBRARY})

# 设置源文件
set(SERVER_FILES multi_udp.cpp)

# 添加可执行文件
add_executable(multi_udp ${SERVER_FILES})

# 链接Winsock库
target_link_libraries(multi_udp ${WS2_32_LIBRARY})

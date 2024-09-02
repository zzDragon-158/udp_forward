cmake_minimum_required(VERSION 3.24)
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
set(CLIENT_FILES client.cpp)

# 添加可执行文件
add_executable(client ${CLIENT_FILES})

# 链接Winsock库
target_link_libraries(client ${WS2_32_LIBRARY})

# 设置源文件
set(C_FILES c.cpp)

# 添加可执行文件
add_executable(c ${C_FILES})

# 链接Winsock库
target_link_libraries(c ${WS2_32_LIBRARY})

# 设置源文件
set(S_FILES s.cpp)

# 添加可执行文件
add_executable(s ${S_FILES})

# 链接Winsock库
target_link_libraries(s ${WS2_32_LIBRARY})
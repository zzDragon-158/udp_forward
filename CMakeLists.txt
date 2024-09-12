cmake_minimum_required(VERSION 3.24)
project(udp_forward)

# 查找Winsock库
find_library(WS2_32_LIBRARY ws2_32)

# 设置源文件
set(PUBLIC_API_FILES public_api.cpp)

# 添加共享库
add_library(public_api ${PUBLIC_API_FILES})

# 链接Winsock库
target_link_libraries(public_api ${WS2_32_LIBRARY})

# 设置源文件
set(SERVER_FILES server.cpp)

# 添加可执行文件
add_executable(server ${SERVER_FILES})

# 链接Winsock库
target_link_libraries(server ${WS2_32_LIBRARY} public_api)

# 设置源文件
set(CLIENT_FILES client.cpp)

# 添加可执行文件
add_executable(client ${CLIENT_FILES})

# 链接Winsock库
target_link_libraries(client ${WS2_32_LIBRARY} public_api)

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

# 设置源文件
set(SV6_FILES sv6.cpp)

# 添加可执行文件
add_executable(sv6 ${SV6_FILES})

# 链接Winsock库
target_link_libraries(sv6 ${WS2_32_LIBRARY})

# 设置源文件
set(CV6_FILES cv6.cpp)

# 添加可执行文件
add_executable(cv6 ${CV6_FILES})

# 链接Winsock库
target_link_libraries(cv6 ${WS2_32_LIBRARY})

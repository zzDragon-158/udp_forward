#pragma once
#pragma comment(lib, "Ws2_32.lib")

#include <bits/stdc++.h>
#include <WS2tcpip.h>

#define LogDebug(fmt, ...)  { \
    char buffer[256]; \
    std::snprintf(buffer, sizeof(buffer), "[%s:%s:%d] " fmt, (strrchr(__FILE__,'\\') != 0? strrchr(__FILE__, '\\')+1: __FILE__), __func__, __LINE__, ##__VA_ARGS__); \
    std::cout << buffer << std::flush; \
}
#define LogError(fmt, ...)  { \
    char buffer[256]; \
    std::snprintf(buffer, sizeof(buffer), "[%s:%s:%d] " fmt, (strrchr(__FILE__,'\\') != 0? strrchr(__FILE__, '\\')+1: __FILE__), __func__, __LINE__, ##__VA_ARGS__); \
    std::cerr << buffer << std::flush; \
}

struct UdpPacket {
    uint8_t sockAddr[32];
    SOCKET sock;
    char* data;
    int dataBytes;
    UdpPacket(): sock(SOCKET_ERROR), data(nullptr), dataBytes(0) {
        memset(&sockAddr, 0, sizeof(sockAddr));
    }
    ~UdpPacket() {
        if (data != nullptr) {
            delete[] data;
        }
    }
};
using UdpPacketPtr = std::shared_ptr<UdpPacket>;
extern bool isWinsockInitialized;

SOCKET createUdpSocket(sockaddr_in& rSockAddr);
SOCKET createUdpSocketV6(sockaddr_in6& rSockAddr);
int sendUdpPacket(SOCKET& sock, sockaddr_in& rSockAddr, UdpPacketPtr& udpPacket);
int sendUdpPacketV6(SOCKET& sock, sockaddr_in6& rSockAddr, UdpPacketPtr& udpPacket);

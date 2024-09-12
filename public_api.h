#pragma once
#pragma comment(lib, "Ws2_32.lib")

#include <bits/stdc++.h>
#include <WS2tcpip.h>

#define LogDebug(fmt,...)   fprintf(stdout, "[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LogError(fmt,...)   fprintf(stderr, "[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

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

SOCKET createUdpSocket(sockaddr_in& rSockAddr);
SOCKET createUdpSocketV6(sockaddr_in6& rSockAddr);
int sendUdpPacket(SOCKET& sock, sockaddr_in& rSockAddr, UdpPacketPtr& udpPacket);
int sendUdpPacketV6(SOCKET& sock, sockaddr_in6& rSockAddr, UdpPacketPtr& udpPacket);

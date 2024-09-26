#include "public_api.h"

#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)

bool isWinsockInitialized = false;

SOCKET createUdpSocket(sockaddr_in& rSockAddr) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LogError("Can not create socket with WSAGetLastError %d.", WSAGetLastError());
        return SOCKET_ERROR;
    }
    /* bind udp socket */ {
        char ipv4Addr[INET_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &rSockAddr.sin_addr, ipv4Addr, sizeof(ipv4Addr));
        if (bind(sock, reinterpret_cast<sockaddr*>(&rSockAddr), sizeof(sockaddr_in)) == SOCKET_ERROR) {
            LogError("can not bind to %s:%u", ipv4Addr, ntohs(rSockAddr.sin_port));
            closesocket(sock);
            return SOCKET_ERROR;
        }
    }
    /* bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(sock, SIO_UDP_CONNRESET, &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), NULL, 0, &dwBytesReturned, NULL, NULL);
    }
    /* print sockaddr */ {
        sockaddr_in sockAddr;
        memset(&sockAddr, 0, sizeof(sockAddr));
        int sockAddrLen = sizeof(sockAddr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrLen);
        LogDebug("create udp socket %u bind to %s:%u",sock, inet_ntoa(sockAddr.sin_addr), ntohs(sockAddr.sin_port));
    }
    return sock;
}

SOCKET createUdpSocketV6(sockaddr_in6& rSockAddr) {
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LogError("Can not create socket with WSAGetLastError %d.", WSAGetLastError());
        return SOCKET_ERROR;
    }
    /* bind udp socket */ {
        char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &rSockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
        if (bind(sock, reinterpret_cast<sockaddr*>(&rSockAddr), sizeof(sockaddr_in6)) == SOCKET_ERROR) {
            LogError("Can not bind to %s:%u with WSAGetLastError %d.", ipv6Addr, ntohs(rSockAddr.sin6_port), WSAGetLastError());
            closesocket(sock);
            return SOCKET_ERROR;
        }
    }
    /* bug: udp socket 10054 */ {
        BOOL bEnalbeConnRestError = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(sock, SIO_UDP_CONNRESET, &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), NULL, 0, &dwBytesReturned, NULL, NULL);
    }
    /* print addr */ {
        sockaddr_in6 sockAddr;
        memset(&sockAddr, 0, sizeof(sockAddr));
        int sockAddrLen = sizeof(sockAddr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrLen);
        char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &sockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
        LogDebug("Create udp socket bind to %s:%u", ipv6Addr, ntohs(sockAddr.sin6_port));
    }
    return sock;
}

int sendUdpPacket(SOCKET& sock, sockaddr_in& rSockAddr, UdpPacketPtr& udpPacket) {
    int sendResult = sendto(sock, udpPacket->data, udpPacket->dataBytes, 0, 
        reinterpret_cast<sockaddr*>(&rSockAddr), sizeof(sockaddr_in));
    if (sendResult == SOCKET_ERROR) {
        LogError("Send failed with WSAGetLastError %d.", WSAGetLastError());
    // } else {
    //     char ipv4Addr[INET_ADDRSTRLEN] = {'\0'};
    //     inet_ntop(AF_INET, &rSockAddr.sin_addr, ipv4Addr, sizeof(ipv4Addr));
    //     LogDebug("Send %d bytes to %s:%u", sendResult, ipv4Addr, ntohs(rSockAddr.sin_port));
    }
    return sendResult;
}

int sendUdpPacketV6(SOCKET& sock, sockaddr_in6& rSockAddr, UdpPacketPtr& udpPacket) {
    int sendResult = sendto(sock, udpPacket->data, udpPacket->dataBytes, 0, 
        reinterpret_cast<sockaddr*>(&rSockAddr), sizeof(sockaddr_in6));
    if (sendResult == SOCKET_ERROR) {
        LogError("Send failed with WSAGetLastError %d.", WSAGetLastError());
    // } else {
    //     char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
    //     inet_ntop(AF_INET6, &rSockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
    //     LogDebug("Send %d bytes to %s:%u", sendResult, ipv6Addr, ntohs(rSockAddr.sin6_port));
    }
    return sendResult;
}

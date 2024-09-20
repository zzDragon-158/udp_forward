#include "server.h"

#define SWITCH_THRESHOLD 5000

extern bool isWinsockInitialized;

Server::Server(uint16_t serverPort, uint16_t remotePort) {
    keepRunning = true;
    // init Winsock
    WSADATA wsaData;
    if (!isWinsockInitialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LogError("WSAStartup failed");
            exit(1);
        } else {
            isWinsockInitialized = true;
            LogDebug("WSAStartup success");
        }
    }
    /* init ctrlSockAddr */ {
        memset(&ctrlSockAddr, 0, sizeof(ctrlSockAddr));
        ctrlSockAddr.sin_family = AF_INET;
        ctrlSockAddr.sin_port = htons(0);
        ctrlSockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    /* init serverAddr */ {
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (initUdpSockets(remotePort)) {
        LogDebug("Init udpSockets success.");
    } else {
        keepRunning = false;
        LogError("Init udpSockets failed.");
    }
}

Server::~Server() {
    for (const SOCKET& sock: udpSockets) {
        closesocket(sock);
    }
}

bool Server::initUdpSockets(uint16_t remotePort) {
    bool ret = true;
    /* init udpSokcetsConnectWithRemoteHost */ {
        sockaddr_in6 remoteAddr;
        memset(&remoteAddr, 0, sizeof(remoteAddr));
        remoteAddr.sin6_family = AF_INET6;
        remoteAddr.sin6_addr = in6addr_any;
        remotePort -= remotePort % 4;
        for (int i = 0; i < 4; ++i) {
            // init remoteAddr
            remoteAddr.sin6_port = htons(remotePort + i);
            // create udp socket
            SOCKET sock = createUdpSocketV6(remoteAddr);
            if (sock == SOCKET_ERROR) {
                LogError("Can not create remote socket %d.", i);
                ret = false;
            }
            udpSokcetsConnectWithRemoteHost.push_back(sock);
            udpSockets.push_back(sock);
        }
    }
    /* init ctrl socket */ {
        ctrlSocket = createUdpSocket(ctrlSockAddr);
        if (ctrlSocket == SOCKET_ERROR) {
            LogError("Can not create ctrl socket.");
            ret = false;
        }
        udpSockets.push_back(ctrlSocket);
    }
    return ret;
}

int Server::receiveUdpPacket() {
    // recv udp packet from remote host
    while (keepRunning) {
        FD_ZERO(&udpSokcetsFDSet);
        for (const SOCKET& sock: udpSockets) {
            FD_SET(sock, &udpSokcetsFDSet);
        }
        if (select(0, &udpSokcetsFDSet, NULL, NULL, NULL) == SOCKET_ERROR) {
            LogError("Select failed with WSAGetLastError %d", WSAGetLastError());
            keepRunning = false;
            break;
        }
        for (const SOCKET& sock: udpSockets) {
            if (FD_ISSET(sock, &udpSokcetsFDSet)) {
                char packetBuffer[65535];
                UdpPacketPtr upp = make_shared<UdpPacket>();
                int sockAddrLen = sizeof(upp->sockAddr);
                upp->dataBytes = recvfrom(
                    sock, packetBuffer, 65535, 0, 
                    reinterpret_cast<sockaddr*>(&upp->sockAddr), &sockAddrLen
                );
                if (upp->dataBytes == SOCKET_ERROR) {
                    LogError("Recv failed with WSAGetLastError %d", WSAGetLastError());
                } else {
                    upp->data = new char[upp->dataBytes];
                    upp->sock = sock;
                    memcpy(upp->data, packetBuffer, upp->dataBytes);
                    /* push to queue */ {
                        lock_guard<mutex> lock(udpPacketQueueMutex);
                        udpPacketQueue.push(upp);
                    }
                    udpPacketQueueCV.notify_one();
                }
            }
        }
    }
    return 0;
}

int Server::forwardUdpPacket() {
    uint64_t udpPacketCount = 0;

    while (keepRunning) {
        unique_lock<mutex> lock(udpPacketQueueMutex);
        udpPacketQueueCV.wait(
            lock, [this] {
                return !udpPacketQueue.empty();
            }
        );
        UdpPacketPtr udpPacket;
        udpPacket = udpPacketQueue.front();
        udpPacketQueue.pop();
        lock.unlock();
        if (udpPacket->sock == ctrlSocket) {
            continue;
        }
        if (udpSokcetConnectWithLocalHost.find(udpPacket->sock) != udpSokcetConnectWithLocalHost.end()) {
            SOCKET sendSock;
            sockaddr_in6 sendSockAddr;
            memset(&sendSockAddr, 0, sizeof(sendSockAddr));
            auto it = socketMapAddr.find(udpPacket->sock);
            if (it != socketMapAddr.end()) {
                sendSockAddr = it->second;
            } else {
                LogError("Can not find remote client addr.");
                continue;
            }
            sendSockAddr.sin6_port += (udpPacketCount / SWITCH_THRESHOLD % 4) << 8;
            sendSock = udpSokcetsConnectWithRemoteHost[udpPacketCount / SWITCH_THRESHOLD % 4];
            ++udpPacketCount;
            sendUdpPacketV6(sendSock, sendSockAddr, udpPacket);
        } else {
            SOCKET sendSocket;
            sockaddr_in sendSockAddr = serverAddr;
            sockaddr_in6 uniqueSockAddr;
            memset(&uniqueSockAddr, 0, sizeof(uniqueSockAddr));
            uniqueSockAddr = *(reinterpret_cast<sockaddr_in6*>(udpPacket->sockAddr));
            uniqueSockAddr.sin6_port -= ((ntohs(uniqueSockAddr.sin6_port) % 4) << 8);
            auto it = addrMapSocket.find(uniqueSockAddr);
            if (it != addrMapSocket.end()) {
                sendSocket = it->second;
            } else {
                LogDebug("uniqueSockAddr.sin6_port = %u", ntohs(uniqueSockAddr.sin6_port));
                sockaddr_in sockAddr;
                memset(&sockAddr, 0, sizeof(sockAddr));
                sockAddr.sin_family = AF_INET;
                sockAddr.sin_port = htons(0);
                sockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                sendSocket = createUdpSocket(sockAddr);
                if (sendSocket != SOCKET_ERROR) {
                    addrMapSocket[uniqueSockAddr] = sendSocket;
                    socketMapAddr[sendSocket] = uniqueSockAddr;
                    udpSokcetConnectWithLocalHost.insert(sendSocket);
                    udpSockets.push_back(sendSocket);
                    sendUdpPacketToCtrl();
                } else {
                    char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
                    inet_ntop(AF_INET6, &uniqueSockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
                    LogError("Can not create udp socket for new client %s:%u.", ipv6Addr, ntohs(uniqueSockAddr.sin6_port));
                    continue;
                }
            }
            sendUdpPacket(sendSocket, sendSockAddr, udpPacket);
        }
        // LogDebug("udpPacketQueue size %d", udpPacketQueue.size());
    }
    return 0;
}

int Server::sendUdpPacketToCtrl()
{
    if (ctrlSockAddr.sin_port == htons(0)) {
        int addrLen = sizeof(ctrlSockAddr);
        if (getsockname(ctrlSocket, reinterpret_cast<sockaddr*>(&ctrlSockAddr), &addrLen) != 0) {
            LogError("Can not get sockname.");
            return SOCKET_ERROR;
        }
    }
    int sendResult = sendto(ctrlSocket, "zzDragon", sizeof("zzDragon"), 0,
        reinterpret_cast<sockaddr*>(&ctrlSockAddr), sizeof(ctrlSockAddr));
    if (sendResult == SOCKET_ERROR) {
        LogError("Send failed with WSAGetLastError %d.", WSAGetLastError());
        return SOCKET_ERROR;
    // } else {
    //     char ipv4Addr[INET_ADDRSTRLEN] = {'\0'};
    //     inet_ntop(AF_INET, &ctrlSockAddr.sin_addr, ipv4Addr, sizeof(ipv4Addr));
    //     LogDebug("Sent %d bytes to %s:%d", sendResult, ipv4Addr, ntohs(ctrlSockAddr.sin_port));
    }
    return 0;
}

int Server::start() {
    thread receiveUdpPacketThread(Server::receiveUdpPacket, this);
    thread forwardUdpPacketThread(Server::forwardUdpPacket, this);
    LogDebug("Server is running.");
    receiveUdpPacketThread.join();
    forwardUdpPacketThread.join();
    LogDebug("Server exit.");
    return 0;
}

int Server::stop() {
    keepRunning = false;
    sendUdpPacketToCtrl();
    return 0;
}

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
    if (initUdpSockets(getUniquePort(remotePort))) {
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
            udpSockets.insert(sock);
        }
    }
    /* init ctrl socket */ {
        ctrlSocket = createUdpSocket(ctrlSockAddr);
        if (ctrlSocket == SOCKET_ERROR) {
            LogError("Can not create ctrl socket.");
            ret = false;
        }
        udpSockets.insert(ctrlSocket);
    }
    return ret;
}

int Server::receiveUdpPacket() {
    while (keepRunning) {
        FD_ZERO(&udpSokcetsFDSet);
        for (const SOCKET& sock: udpSockets) {
            FD_SET(sock, &udpSokcetsFDSet);
        }
        if (select(0, &udpSokcetsFDSet, NULL, NULL, NULL) == SOCKET_ERROR) {
            LogError("Select failed with WSAGetLastError %d", WSAGetLastError());
            continue;
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
                    continue;
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
            SOCKET sendSocket;
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
            sendSocket = udpSokcetsConnectWithRemoteHost[udpPacketCount / SWITCH_THRESHOLD % 4];
            ++udpPacketCount;
            sendUdpPacketV6(sendSocket, sendSockAddr, udpPacket);
        } else {
            SOCKET sendSocket;
            sockaddr_in sendSockAddr = serverAddr;
            sockaddr_in6 uniqueSockAddr;
            memset(&uniqueSockAddr, 0, sizeof(uniqueSockAddr));
            uniqueSockAddr = *(reinterpret_cast<sockaddr_in6*>(udpPacket->sockAddr));
            uniqueSockAddr.sin6_port = htons(getUniquePort(ntohs(uniqueSockAddr.sin6_port)));
            auto it = addrMapSocket.find(uniqueSockAddr);
            if (it != addrMapSocket.end()) {
                sendSocket = it->second;
            } else {
                sockaddr_in sockAddr;
                memset(&sockAddr, 0, sizeof(sockAddr));
                sockAddr.sin_family = AF_INET;
                sockAddr.sin_port = htons(0);
                sockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                sendSocket = createUdpSocket(sockAddr);
                char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
                inet_ntop(AF_INET6, &uniqueSockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
                if (sendSocket != SOCKET_ERROR) {
                    LogDebug("Create udp socket %u for new client from %s:%u.", sendSocket, ipv6Addr, ntohs(uniqueSockAddr.sin6_port));
                    addrMapSocket[uniqueSockAddr] = sendSocket;
                    socketMapAddr[sendSocket] = uniqueSockAddr;
                    socketLifeCycle[sendSocket] = 60;
                    udpSokcetConnectWithLocalHost.insert(sendSocket);
                    udpSockets.insert(sendSocket);
                    sendUdpPacketToCtrl();
                } else {
                    LogError("Can not create udp socket for new client from %s:%u.", ipv6Addr, ntohs(uniqueSockAddr.sin6_port));
                    continue;
                }
            }
            if (socketLifeCycle.find(sendSocket) != socketLifeCycle.end()) socketLifeCycle[sendSocket] = 60;
            sendUdpPacket(sendSocket, sendSockAddr, udpPacket);
        }
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
    }
    return 0;
}

int Server::cleanUselessSockets() {
    if (keepRunning) {
        for (auto it = socketLifeCycle.begin(); it != socketLifeCycle.end();) {
            it->second -= 1;
            if (it->second <= 0) {
                udpSokcetConnectWithLocalHost.erase(it->first);
                udpSockets.erase(it->first);
                auto sockAddr = socketMapAddr[it->first];
                socketMapAddr.erase(it->first);
                addrMapSocket.erase(sockAddr);
                LogDebug("Close UDP Socket: %u because this socket is inactive.", it->first);
                closesocket(it->first);
                it = socketLifeCycle.erase(it);
            } else {
                ++it;
            }
        }
        sendUdpPacketToCtrl();
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

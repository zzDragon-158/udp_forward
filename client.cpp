#include "client.h"

#define SWITCH_THRESHOLD 5000

extern bool isWinsockInitialized;

Client::Client(const char* serverIPv6Addr, uint16_t serverPort, uint16_t remotePort, uint16_t localPort) {
    sendUdpPacketCount = 0;
    recvUdpPacketCount = 0;
    keepRunning = true;
    // init Winsock
    WSADATA wsaData;
    if (!isWinsockInitialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            LogError("WSAStartup failed");
            keepRunning = false;
        } else {
            isWinsockInitialized = true;
            LogDebug("WSAStartup success");
        }
    }
    initSockAddr(serverIPv6Addr, serverPort);
    if (initUdpSockets(remotePort, localPort)) {
        LogDebug("Init udpSockets success.");
    } else {
        keepRunning = false;
        LogError("Init udpSockets failed.");
    }
}

Client::~Client() {
    for (const SOCKET& sock: udpSockets) {
        closesocket(sock);
    }
}

void Client::initSockAddr(const char* serverIPv6Addr, uint16_t serverPort) {
    /* init serverAddrVector */ {
        sockaddr_in6 serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, serverIPv6Addr, &serverAddr.sin6_addr);
        for (int i = 0; i < 4; ++i) {
            // init serverAddr
            serverAddr.sin6_port = htons(serverPort + i);
            serverAddrVector.push_back(serverAddr);
        }
    }
    /* init clientAddr */ {
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(0);
        clientAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    /* init ctrlSockAddr */ {
        memset(&ctrlSockAddr, 0, sizeof(ctrlSockAddr));
        ctrlSockAddr.sin_family = AF_INET;
        ctrlSockAddr.sin_port = htons(0);
        ctrlSockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
}

bool Client::initUdpSockets(uint16_t remotePort, uint16_t localPort) {
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
                keepRunning = false;
                LogError("Can not create remote socket %d.", i);
                ret = false;
            }
            udpSokcetsConnectWithRemoteHost.push_back(sock);
            udpSockets.push_back(sock);
        }
    }
    /* init udpSokcetConnectWithLocalHost */ {
        // init localAddr
        sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        localAddr.sin_port = htons(localPort);
        // create udp socket
        SOCKET sock = createUdpSocket(localAddr);
        if (sock == SOCKET_ERROR) {
            keepRunning = false;
            LogError("Can not create local socket.");
            ret = false;
        }
        udpSokcetConnectWithLocalHost = sock;
        udpSockets.push_back(sock);
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

int Client::receiveUdpPacket() {
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
                } else {
                    if (sock != ctrlSocket && sock != udpSokcetConnectWithLocalHost) ++recvUdpPacketCount;
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

int Client::forwardUdpPacket() {
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
        if (udpPacket->sock == udpSokcetConnectWithLocalHost) {
            SOCKET sendSocket = udpSokcetsConnectWithRemoteHost[sendUdpPacketCount / SWITCH_THRESHOLD % 4];
            sockaddr_in6 sendAddr = serverAddrVector[sendUdpPacketCount / SWITCH_THRESHOLD % 4];
            sockaddr_in recvSockAddr = *(reinterpret_cast<sockaddr_in*>(&udpPacket->sockAddr));
            if (clientAddr.sin_port != recvSockAddr.sin_port) {
                clientAddr = recvSockAddr;
                LogDebug("New client connected, port: %u", ntohs(clientAddr.sin_port));
            }
            ++sendUdpPacketCount;
            sendUdpPacketV6(sendSocket, sendAddr, udpPacket);
        } else {
            SOCKET sendSocket = udpSokcetConnectWithLocalHost;
            sockaddr_in sendAddr = clientAddr;
            sendUdpPacket(sendSocket, sendAddr, udpPacket);
        }
    }
    return 0;
}

int Client::sendUdpPacketToCtrl()
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

int Client::start() {
    thread recvUdpPacketFromRemoteHostThread(Client::receiveUdpPacket, this);
    thread forwardPacketThread(Client::forwardUdpPacket, this);
    LogDebug("Client is running");
    recvUdpPacketFromRemoteHostThread.join();
    forwardPacketThread.join();
    LogDebug("Client exit");
    return 0;
}

int Client::stop() {
    keepRunning = false;
    sendUdpPacketToCtrl();
    return 0;
}

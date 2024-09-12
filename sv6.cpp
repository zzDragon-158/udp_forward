#include <bits/stdc++.h>
#include <WS2tcpip.h>   // 包含Winsock2和网络相关的头文件
#pragma comment(lib, "Ws2_32.lib")  // 链接Ws2_32.lib库文件

using namespace std;
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#define LogDebug(fmt,...)   fprintf(stdout, "[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LogError(fmt,...)   fprintf(stderr, "[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
bool isWinsockInitialized = false;
struct UdpPacket
{
    sockaddr_in6 addr;
    SOCKET sock;
    char* data;
    int dataBytes;
    UdpPacket() : sock(SOCKET_ERROR), data(nullptr), dataBytes(0) {
        memset(&addr, 0, sizeof(addr));
    }
    ~UdpPacket() {
        if (data != nullptr) {
            delete[] data;
        }
    }
};
using UdpPacketPtr = shared_ptr<UdpPacket>;
struct sockaddr_in6_compare {
    bool operator()(const sockaddr_in6& addr1, const sockaddr_in6& addr2) const {
        if (memcmp(&addr1.sin6_addr, &addr2.sin6_addr, sizeof(in6_addr)) != 0)
            return memcmp(&addr1.sin6_addr, &addr2.sin6_addr, sizeof(in6_addr)) < 0;
        return addr1.sin6_port < addr2.sin6_port;
    }
};

SOCKET createUdpSocketV6(sockaddr_in6* pAddr)
{
    SOCKET sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        LogError("Can not create socket with WSAGetLastError %d.", WSAGetLastError());
        return SOCKET_ERROR;
    }
    /* bind udp socket */ {
        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        if (pAddr != nullptr) {
            addr = *pAddr;
        } else {
            addr.sin6_family = AF_INET6;
            addr.sin6_addr = in6addr_loopback;
            addr.sin6_port = htons(0);
        }
        char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &addr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in6)) == SOCKET_ERROR) {
            LogError("Can not bind to %s:%u with WSAGetLastError %d.", ipv6Addr, ntohs(addr.sin6_port), WSAGetLastError());
            closesocket(sock);
            return SOCKET_ERROR;
        }
    }
    // 10054 bug
    BOOL bEnalbeConnRestError = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), \
            NULL, 0, &dwBytesReturned, NULL, NULL);
    /* print addr */ {
        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        int addrLen = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &addr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
        LogDebug("Create udp socket bind to %s:%u", ipv6Addr, ntohs(addr.sin6_port));
    }
    return sock;
}

int sendUdpPacketV6(SOCKET sock, sockaddr_in6* pAddr, UdpPacketPtr udpPacket) {
    int sendResult = sendto(sock, udpPacket->data, udpPacket->dataBytes, 0, 
        reinterpret_cast<sockaddr*>(pAddr), sizeof(sockaddr_in6));
    if (sendResult == SOCKET_ERROR) {
        LogError("Send failed with WSAGetLastError %d.", WSAGetLastError());
    } else {
        char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
        inet_ntop(AF_INET6, &pAddr->sin6_addr, ipv6Addr, sizeof(ipv6Addr));
        LogDebug("Send %d bytes to %s:%u", sendResult, ipv6Addr, ntohs(pAddr->sin6_port));
    }
    return sendResult;
}

class Server {
public:
    Server();
    ~Server();
    bool initUdpSockets();
    int receiveUdpPacket();
    int forwardUdpPacket();
    int sendUdpPacketToCtrl(SOCKET sock);
    int run();
private:
    unordered_set<SOCKET> udpSokcetConnectWithLocalHost;
    vector<SOCKET> udpSokcetsConnectWithRemoteHost;
    SOCKET ctrlSocket;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    condition_variable udpSokcetsFDSetCV;
    vector<sockaddr_in6> serverAddrVector;
    sockaddr_in6 serverAddr;     // 127.0.0.1:27015
    queue<UdpPacketPtr> udpPacketQueue;
    mutex udpPacketQueueMutex;
    condition_variable udpPacketQueueCV;
    map<sockaddr_in6, SOCKET, sockaddr_in6_compare> addrMapSocket;
    map<SOCKET, sockaddr_in6> socketMapAddr;
    bool keepRunning;
};

Server::Server() {
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
    /* init serverAddr */ {
        serverAddr.sin6_family = AF_INET6;
        serverAddr.sin6_addr = in6addr_loopback;
        serverAddr.sin6_port = htons(27015);
    }
    if (initUdpSockets()) {
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
    WSACleanup();
}

bool Server::initUdpSockets() {
    bool ret = true;
    /* init udpSokcetsConnectWithRemoteHost */ {
        sockaddr_in6 remoteAddr;
        memset(&remoteAddr, 0, sizeof(remoteAddr));
        remoteAddr.sin6_family = AF_INET6;
        remoteAddr.sin6_addr = in6addr_loopback;
        for (int i = 0; i < 4; ++i) {
            // init remoteAddr
            remoteAddr.sin6_port = htons(8905+i);
            // create udp socket
            SOCKET sock = createUdpSocketV6(&remoteAddr);
            if (sock == SOCKET_ERROR) {
                LogError("Can not create remote socket %d.", i);
                ret = false;
            }
            udpSokcetsConnectWithRemoteHost.push_back(sock);
            udpSockets.push_back(sock);
        }
    }
    /* init ctrl socket */ {
        ctrlSocket = createUdpSocketV6(nullptr);
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
                int addrLen = sizeof(sockaddr_in6);
                upp->dataBytes = recvfrom(
                    sock, packetBuffer, 65535, 0, 
                    reinterpret_cast<sockaddr*>(&upp->addr), &addrLen
                );
                if (upp->dataBytes == SOCKET_ERROR) {
                    LogError("Recv failed with WSAGetLastError %d", WSAGetLastError());
                } else {
                    if (sock == ctrlSocket) {
                        continue;
                    }
                    upp->data = new char[upp->dataBytes];
                    upp->sock = sock;
                    memcpy(upp->data, packetBuffer, upp->dataBytes);
                    /* push to queue */ {
                        lock_guard<mutex> lock(udpPacketQueueMutex);
                        udpPacketQueue.push(upp);
                        udpPacketQueueCV.notify_one();
                    }
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
        SOCKET sendSocket;
        sockaddr_in6 sendAddr;
        memset(&sendAddr, 0, sizeof(sendAddr));
        if (udpSokcetConnectWithLocalHost.find(udpPacket->sock) != udpSokcetConnectWithLocalHost.end()) {
            auto it = socketMapAddr.find(udpPacket->sock);
            if (it != socketMapAddr.end()) {
                sendAddr = it->second;
            } else {
                LogError("Can not find remote client addr.");
                continue;
            }
            sendAddr.sin6_port -= (udpPacketCount / 65536 % 4) << 8;
            sendSocket = udpSokcetsConnectWithRemoteHost[udpPacketCount / 65536 % 4];
            ++udpPacketCount;
        } else {
            sockaddr_in6 uniqueAddr;
            memset(&uniqueAddr, 0, sizeof(uniqueAddr));
            uniqueAddr = udpPacket->addr;
            uniqueAddr.sin6_port = udpPacket->addr.sin6_port + ((3 - ntohs(udpPacket->addr.sin6_port % 4)) << 8);
            auto it = addrMapSocket.find(uniqueAddr);
            if (it != addrMapSocket.end()) {
                sendSocket = it->second;
            } else {
                sendSocket = createUdpSocketV6(nullptr);
                if (sendSocket != SOCKET_ERROR) {
                    addrMapSocket[udpPacket->addr] = sendSocket;
                    socketMapAddr[sendSocket] = udpPacket->addr;
                    udpSokcetConnectWithLocalHost.insert(sendSocket);
                    udpSockets.push_back(sendSocket);
                    sendUdpPacketToCtrl(sendSocket);
                } else {
                    char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
                    inet_ntop(AF_INET6, &uniqueAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
                    LogError("Can not create udp socket for new client %s:%u.", ipv6Addr, ntohs(uniqueAddr.sin6_port));
                    continue;
                }
            }
            sendAddr = serverAddr;
        }
        sendUdpPacketV6(sendSocket, &sendAddr, udpPacket);
        LogDebug("udpPacketQueue size %d", udpPacketQueue.size());
    }
    return 0;
}

int Server::sendUdpPacketToCtrl(SOCKET sock)
{
    int ret = 0;
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    int addrLen = sizeof(addr);
    if (getsockname(ctrlSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0) {
        int sendResult = sendto(sock, "zzDragon", sizeof("zzDragon"), 0, 
            reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in6));
        if (sendResult == SOCKET_ERROR) {
            LogError("Send failed with WSAGetLastError %d.", WSAGetLastError());
            ret = -1;
        } else {
            char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
            inet_ntop(AF_INET6, &addr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
            LogDebug("Sent %d bytes to %s:%d", sendResult, ipv6Addr, ntohs(addr.sin6_port));
        }
    } else {
        LogError("Can not get sockname.");
        ret = -1;
    };

    return ret;
}

int Server::run() {
    thread receiveUdpPacketThread(Server::receiveUdpPacket, this);
    thread forwardUdpPacketThread(Server::forwardUdpPacket, this);
    receiveUdpPacketThread.join();
    forwardUdpPacketThread.join();
    LogDebug("Server exit.");
    return 0;
}

int main() {
    Server server;
    server.run();
    return 0;
}

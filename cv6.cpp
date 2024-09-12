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

class Client {
public:
    Client();
    ~Client();
    bool initUdpSockets();
    int receiveUdpPacket();
    int forwardUdpPacket();
    int run();
private:
    SOCKET udpSokcetConnectWithLocalHost;
    vector<SOCKET> udpSokcetsConnectWithRemoteHost;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    vector<sockaddr_in6> serverAddrVector;
    sockaddr_in6 clientAddr;
    queue<UdpPacketPtr> udpPacketQueue;
    mutex udpPacketQueueMutex;
    condition_variable udpPacketQueueCV;
    bool keepRunning;
};

Client::Client() {
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
    /* init serverAddrVector */ {
        sockaddr_in6 serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin6_family = AF_INET6;
        serverAddr.sin6_addr = in6addr_loopback;
        for (int i = 0; i < 4; ++i) {
            // init serverAddr
            serverAddr.sin6_port = htons(8905+i);
            serverAddrVector.push_back(serverAddr);
        }
    }
    if (initUdpSockets()) {
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
    WSACleanup();
}

bool Client::initUdpSockets() {
    bool ret = true;
    /* init udpSokcetsConnectWithRemoteHost */ {
        sockaddr_in6 remoteAddr;
        memset(&remoteAddr, 0, sizeof(remoteAddr));
        remoteAddr.sin6_family = AF_INET6;
        remoteAddr.sin6_addr = in6addr_loopback;
        for (int i = 0; i < 4; ++i) {
            // init remoteAddr
            remoteAddr.sin6_port = htons(3461+i);
            // create udp socket
            SOCKET sock = createUdpSocketV6(&remoteAddr);
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
        sockaddr_in6 localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin6_family = AF_INET6;
        localAddr.sin6_addr = in6addr_loopback;
        localAddr.sin6_port = htons(3460);
        // create udp socket
        SOCKET sock = createUdpSocketV6(&localAddr);
        if (sock == SOCKET_ERROR) {
            keepRunning = false;
            LogError("Can not create local socket.");
            ret = false;
        }
        udpSokcetConnectWithLocalHost = sock;
        udpSockets.push_back(sock);
    }

    return ret;
}

/**
 * 函数名：recvUdpPacketFromRemoteHost
 * 功能：从远程主机接收 Udp 数据包
 * 参数：无
 * 返回值：SOCKET，成功返回 0，失败返回 SOCKET_ERROR
 * 异常：如果创建套接字失败，该线程将打印错误信息并直接返回
 */
int Client::receiveUdpPacket() {
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

int Client::forwardUdpPacket() {
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
        if (udpPacket->sock == udpSokcetConnectWithLocalHost) {
            sendSocket = udpSokcetsConnectWithRemoteHost[udpPacketCount / 65536 % 4];
            sendAddr = serverAddrVector[udpPacketCount / 65536 % 4];
            clientAddr = udpPacket->addr;
            LogDebug("new client connected, port: %u", ntohs(clientAddr.sin6_port));
            ++udpPacketCount;
        } else {
            sendSocket = udpSokcetConnectWithLocalHost;
            sendAddr = clientAddr;
        }
        sendUdpPacketV6(sendSocket, &sendAddr, udpPacket);
        LogDebug("udpPacketQueue size %d", udpPacketQueue.size());
    }
    return 0;
}

int Client::run() {
    thread recvUdpPacketFromRemoteHostThread(Client::receiveUdpPacket, this);
    thread forwardPacketThread(Client::forwardUdpPacket, this);
    recvUdpPacketFromRemoteHostThread.join();
    forwardPacketThread.join();
    LogDebug("Client exit.");
    return 0;
}

int main() {
    Client client;
    client.run();
    return 0;
}

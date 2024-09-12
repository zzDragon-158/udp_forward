#include "public_api.h"

using namespace std;
bool isWinsockInitialized = false;

struct sockaddr_in6_compare {
    bool operator()(const sockaddr_in6& addr1, const sockaddr_in6& addr2) const {
        if (memcmp(&addr1.sin6_addr, &addr2.sin6_addr, sizeof(in6_addr)) != 0)
            return memcmp(&addr1.sin6_addr, &addr2.sin6_addr, sizeof(in6_addr)) < 0;
        return addr1.sin6_port < addr2.sin6_port;
    }
};

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
    sockaddr_in ctrlSockAddr;
    SOCKET ctrlSocket;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    condition_variable udpSokcetsFDSetCV;
    sockaddr_in serverAddr;
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
    /* init ctrlSockAddr */ {
        memset(&ctrlSockAddr, 0, sizeof(ctrlSockAddr));
        ctrlSockAddr.sin_family = AF_INET;
        ctrlSockAddr.sin_port = htons(0);
        ctrlSockAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    /* init serverAddr */ {
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(27015);
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
            sendSockAddr.sin6_port -= (udpPacketCount / 65536 % 4) << 8;
            sendSock = udpSokcetsConnectWithRemoteHost[udpPacketCount / 65536 % 4];
            ++udpPacketCount;
            sendUdpPacketV6(sendSock, sendSockAddr, udpPacket);
        } else {
            SOCKET sendSocket;
            sockaddr_in sendSockAddr = serverAddr;
            sockaddr_in6 uniqueSockAddr;
            memset(&uniqueSockAddr, 0, sizeof(uniqueSockAddr));
            uniqueSockAddr = *(reinterpret_cast<sockaddr_in6*>(udpPacket->sockAddr));
            uniqueSockAddr.sin6_port += ((3 - ntohs(uniqueSockAddr.sin6_port % 4)) << 8);
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
                if (sendSocket != SOCKET_ERROR) {
                    addrMapSocket[uniqueSockAddr] = sendSocket;
                    socketMapAddr[sendSocket] = uniqueSockAddr;
                    udpSokcetConnectWithLocalHost.insert(sendSocket);
                    udpSockets.push_back(sendSocket);
                    sendUdpPacketToCtrl(sendSocket);
                } else {
                    char ipv6Addr[INET6_ADDRSTRLEN] = {'\0'};
                    inet_ntop(AF_INET6, &uniqueSockAddr.sin6_addr, ipv6Addr, sizeof(ipv6Addr));
                    LogError("Can not create udp socket for new client %s:%u.", ipv6Addr, ntohs(uniqueSockAddr.sin6_port));
                    continue;
                }
            }
            sendUdpPacket(sendSocket, sendSockAddr, udpPacket);
        }
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

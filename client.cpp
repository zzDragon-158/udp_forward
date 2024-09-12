#include "public_api.h"

using namespace std;
bool isWinsockInitialized = false;

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
    sockaddr_in clientAddr;
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
    /* init clientAddr */ {
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(0);
        clientAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
        localAddr.sin_port = htons(3460);
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
        if (udpPacket->sock == udpSokcetConnectWithLocalHost) {
            SOCKET sendSocket = udpSokcetsConnectWithRemoteHost[udpPacketCount / 65536 % 4];
            sockaddr_in6 sendAddr = serverAddrVector[udpPacketCount / 65536 % 4];
            sockaddr_in recvSockAddr = *(reinterpret_cast<sockaddr_in*>(&udpPacket->sockAddr));
            if (clientAddr.sin_port != recvSockAddr.sin_port) {
                clientAddr = recvSockAddr;
                LogDebug("new client connected, port: %u", ntohs(clientAddr.sin_port));
            }
            ++udpPacketCount;
            sendUdpPacketV6(sendSocket, sendAddr, udpPacket);
        } else {
            SOCKET sendSocket = udpSokcetConnectWithLocalHost;
            sockaddr_in sendAddr = clientAddr;
            sendUdpPacket(sendSocket, sendAddr, udpPacket);
        }
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

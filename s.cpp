#include <bits/stdc++.h>
#include <WS2tcpip.h>   // 包含Winsock2和网络相关的头文件
#pragma comment(lib, "Ws2_32.lib")  // 链接Ws2_32.lib库文件

using namespace std;
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
bool isWinsockInitialized = false;
struct UdpPacket
{
    sockaddr_in addr;
    SOCKET sock;
    char* data;
    int dataBytes;
    ~UdpPacket() {
        if (data != nullptr) {
            delete[] data;
        }
    }
};
using UdpPacketPtr = shared_ptr<UdpPacket>;
struct sockaddr_in_compare {
    bool operator()(const sockaddr_in& lhs, const sockaddr_in& rhs) const {
        if (lhs.sin_addr.s_addr != rhs.sin_addr.s_addr) {
            return lhs.sin_addr.s_addr < rhs.sin_addr.s_addr;
        }
        return lhs.sin_port < rhs.sin_port;
    }
};

SOCKET createUdpSocket(sockaddr_in* pAddr)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        cerr << __func__ << ": can't create socket! error code " << WSAGetLastError() << endl;
        return SOCKET_ERROR;
    }
    /* bind udp socket */ {
        sockaddr_in addr;
        if (pAddr != nullptr) {
            addr = *pAddr;
        } else {
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(0);
        }
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in)) == SOCKET_ERROR) {
            cerr << "can not bind to " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
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
        sockaddr_in addr;
        int addrLen = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        cout << "create udp socket bind to " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
    }

    return sock;
}

int sendUdpPacket(SOCKET sock, sockaddr_in* pAddr, UdpPacketPtr udpPacket) {
    int sendResult = sendto(sock, udpPacket->data, udpPacket->dataBytes, 0, 
        reinterpret_cast<sockaddr*>(pAddr), sizeof(sockaddr_in));
    std::cout << "Sent " << sendResult << " bytes to "
        << inet_ntoa(pAddr->sin_addr) << ":" << ntohs(pAddr->sin_port)
        << " code " << WSAGetLastError() << std::endl;
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
    vector<SOCKET> udpSokcetConnectWithRemoteHost;
    SOCKET ctrlSocket;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    condition_variable udpSokcetsFDSetCV;
    vector<sockaddr_in> serverAddrVector;
    sockaddr_in serverAddr;     // 127.0.0.1:27015
    queue<UdpPacketPtr> udpPacketQueue;
    mutex udpPacketQueueMutex;
    condition_variable udpPacketQueueCV;
    map<sockaddr_in, SOCKET, sockaddr_in_compare> addrMapSocket;
    map<SOCKET, sockaddr_in> socketMapAddr;
    bool keepRunning;
};

Server::Server() {
    keepRunning = true;
    // init Winsock
    WSADATA wsaData;
    if (!isWinsockInitialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            cerr << "Failed to initialize Winsock." << std::endl;
            exit(1);
        } else {
            isWinsockInitialized = true;
            std::cout << "init winsock success" << std::endl;
        }
    }
    /* init serverAddr */ {
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serverAddr.sin_port = htons(27015);
    }
    if (initUdpSockets()) {
        std::cout << "init udpSockets success" << std::endl;
    } else {
        keepRunning = false;
        std::cout << "init udpSockets failed" << std::endl;
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
        sockaddr_in remoteAddr;
        remoteAddr.sin_family = AF_INET;
        remoteAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        for (int i = 0; i < 4; ++i) {
            // init remoteAddr
            remoteAddr.sin_port = htons(8905+i);
            // create udp socket
            SOCKET sock = createUdpSocket(&remoteAddr);
            if (sock == SOCKET_ERROR) {
                keepRunning = false;
                std::cout << "init udpSocketsConnectWithRemoteHost failed" << std::endl;
                ret = false;
            }
            udpSokcetConnectWithRemoteHost.push_back(sock);
            udpSockets.push_back(sock);
        }
    }
    /* init ctrl socket */ {
        ctrlSocket = createUdpSocket(nullptr);
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
            std::cerr << "Select failed with error: " << WSAGetLastError() << "\n";
            keepRunning = false;
            break;
        }
        for (const SOCKET& sock: udpSockets) {
            if (FD_ISSET(sock, &udpSokcetsFDSet)) {
                char packetBuffer[65535];
                UdpPacketPtr upp = make_shared<UdpPacket>();
                int addrLen = sizeof(sockaddr_in);
                upp->dataBytes = recvfrom(
                    sock, packetBuffer, 65535, 0, 
                    reinterpret_cast<sockaddr*>(&upp->addr), &addrLen
                );
                if (upp->dataBytes == SOCKET_ERROR) {
                    cerr << __func__ <<": recv failed! error code " << WSAGetLastError() << std::endl;
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
        sockaddr_in sendAddr;
        if (udpSokcetConnectWithLocalHost.find(udpPacket->sock) != udpSokcetConnectWithLocalHost.end()) {
            auto it = socketMapAddr.find(udpPacket->sock);
            if (it != socketMapAddr.end()) {
                sendAddr = it->second;
            } else {
                std::cout << "can not find remote client addr" << std::endl;
                continue;
            }
            sendAddr.sin_port -= (udpPacketCount / 65536 % 4) << 8;
            sendSocket = udpSokcetConnectWithRemoteHost[udpPacketCount / 65536 % 4];
            ++udpPacketCount;
        } else {
            sockaddr_in uniqueAddr = udpPacket->addr;
            uniqueAddr.sin_port = udpPacket->addr.sin_port + ((3 - ntohs(udpPacket->addr.sin_port % 4)) << 8);
            auto it = addrMapSocket.find(uniqueAddr);
            if (it != addrMapSocket.end()) {
                sendSocket = it->second;
            } else {
                sendSocket = createUdpSocket(nullptr);
                if (sendSocket != SOCKET_ERROR) {
                    addrMapSocket[udpPacket->addr] = sendSocket;
                    socketMapAddr[sendSocket] = udpPacket->addr;
                    udpSokcetConnectWithLocalHost.insert(sendSocket);
                    udpSockets.push_back(sendSocket);
                    sendUdpPacketToCtrl(sendSocket);
                } else {
                    std::cout << "create udp socket failed" << std::endl;
                    continue;
                }
            }
            sendAddr = serverAddr;
        }
        sendUdpPacket(sendSocket, &sendAddr, udpPacket);
        std::cout << "udpPacketQueue size: " << udpPacketQueue.size() << std::endl;
    }
    return 0;
}

int Server::sendUdpPacketToCtrl(SOCKET sock)
{
    int ret = 0;
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getsockname(ctrlSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0) {
        int sendResult = sendto(sock, "zzDragon", sizeof("zzDragon"), 0, 
            reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in));
        if (sendResult == SOCKET_ERROR) {
            std::cout << "send to ctrl failed" << std::endl;
            ret = -1;
        } else {
            std::cout << "Sent " << sendResult << " bytes to "
                << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port)
                << " code " << WSAGetLastError() << std::endl;
        }
    } else {
        std::cout << "can not get sockname" << std::endl;
        ret = -1;
    };

    return ret;
}

int Server::run() {
    thread recvUdpPacketFromRemoteHostThread(Server::receiveUdpPacket, this);
    thread forwardPacketThread(Server::forwardUdpPacket, this);
    recvUdpPacketFromRemoteHostThread.join();
    forwardPacketThread.join();
    std::cout << "run complete" << std::endl;
    return 0;
}

int main() {
    Server server;
    server.run();
    return 0;
}

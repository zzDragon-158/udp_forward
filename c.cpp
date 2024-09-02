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
    /* print addr */ {
        sockaddr_in addr;
        int addrLen = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        cout << "create udp socket bind to " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
    }

    return sock;
}

class Client {
public:
    Client();
    ~Client();
    bool initUdpSockets();
    int recvUdpPacket();
    int forwardUdpPacket();
    int sendUdpPacket(SOCKET sock, sockaddr_in* pAddr, UdpPacketPtr udpPacket);
    int run();
private:
    SOCKET udpSokcetConnectWithLocalHost;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    vector<sockaddr_in> serverAddrVector;
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
            cerr << "Failed to initialize Winsock." << endl;
            keepRunning = false;
        } else {
            isWinsockInitialized = true;
            cout << "init winsock success" << endl;
        }
    }
    /* init serverAddrVector */ {
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        for (int i = 0; i < 4; ++i) {
            // init serverAddr
            serverAddr.sin_port = htons(8905+i);
            serverAddrVector.push_back(serverAddr);
        }
    }
    if (initUdpSockets()) {
        cout << "init udpSockets success" << endl;
    } else {
        keepRunning = false;
        cout << "init udpSockets failed" << endl;
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
    // 10054 bug
    BOOL bEnalbeConnRestError = FALSE;
    DWORD dwBytesReturned = 0;
    /* init udpSokcetsConnectWithRemoteHost */ {
        sockaddr_in remoteAddr;
        remoteAddr.sin_family = AF_INET;
        remoteAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        for (int i = 0; i < 4; ++i) {
            // init remoteAddr
            remoteAddr.sin_port = htons(3461+i);
            // create udp socket
            SOCKET sock = createUdpSocket(&remoteAddr);
            if (sock == SOCKET_ERROR) {
                keepRunning = false;
                cout << "init udpSocketsConnectWithRemoteHost failed" << endl;
                ret = false;
            }
            WSAIoctl(sock, SIO_UDP_CONNRESET, &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), \
                NULL, 0, &dwBytesReturned, NULL, NULL);
            udpSockets.push_back(sock);
        }
    }

    /* init udpSokcetConnectWithLocalHost */ {
        // init localAddr
        sockaddr_in localAddr;
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        localAddr.sin_port = htons(3460);
        // create udp socket
        SOCKET sock = createUdpSocket(&localAddr);
        if (sock == SOCKET_ERROR) {
            keepRunning = false;
            cout << "init udpSokcetConnectWithLocalHost failed" << endl;
            ret = false;
        }
        WSAIoctl(sock, SIO_UDP_CONNRESET, &bEnalbeConnRestError, sizeof(bEnalbeConnRestError), \
            NULL, 0, &dwBytesReturned, NULL, NULL);
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
int Client::recvUdpPacket() {
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
                    cerr << __func__ <<": recv failed! error code " << WSAGetLastError() << endl;
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
        sockaddr_in sendAddr;
        if (udpPacket->sock == udpSokcetConnectWithLocalHost) {
            sendSocket = udpSockets[udpPacketCount / 65536 % udpSockets.size()];
            sendAddr = serverAddrVector[udpPacketCount / 65536 % serverAddrVector.size()];
            clientAddr = udpPacket->addr;
            ++udpPacketCount;
        } else {
            sendSocket = udpSokcetConnectWithLocalHost;
            sendAddr = clientAddr;
        }
        sendUdpPacket(sendSocket, &sendAddr, udpPacket);
        cout << "udpPacketQueue size: " << udpPacketQueue.size() << endl;
    }
    return 0;
}

int Client::sendUdpPacket(SOCKET sock, sockaddr_in* pAddr, UdpPacketPtr udpPacket) {
    int sendResult = sendto(sock, udpPacket->data, udpPacket->dataBytes, 0, 
        reinterpret_cast<sockaddr*>(pAddr), sizeof(sockaddr_in));
    cout << "Sent " << sendResult << " bytes to "
        << inet_ntoa(pAddr->sin_addr) << ":" << ntohs(pAddr->sin_port)
        << " code " << WSAGetLastError() << endl;
    return sendResult;
}

int Client::run() {
    thread recvUdpPacketFromRemoteHostThread(Client::recvUdpPacket, this);
    thread forwardPacketThread(Client::forwardUdpPacket, this);
    recvUdpPacketFromRemoteHostThread.join();
    forwardPacketThread.join();
    cout << "run complete" << endl;
    return 0;
}

int main() {
    Client client;
    client.run();
    return 0;
}

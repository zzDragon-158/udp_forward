#pragma once

#include "public_api.h"

using namespace std;
struct sockaddr_in6_compare {
    bool operator()(const sockaddr_in6& lhs, const sockaddr_in6& rhs) const {
        // Compare the port first
        if (lhs.sin6_port != rhs.sin6_port) {
            return lhs.sin6_port < rhs.sin6_port;
        }
        // Compare the address
        return std::memcmp(&lhs.sin6_addr, &rhs.sin6_addr, sizeof(lhs.sin6_addr)) < 0;
    }
};

class Server {
public:
    Server(uint16_t serverPort, uint16_t remotePort);
    ~Server();
    bool initUdpSockets(uint16_t remotePort);
    int receiveUdpPacket();
    int forwardUdpPacket();
    int sendUdpPacketToCtrl();
    int start();
    int stop();
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
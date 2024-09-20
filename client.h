#pragma once

#include "public_api.h"

using namespace std;

class Client {
public:
    Client(const char* serverIPv6Addr, uint16_t serverPort, uint16_t remotePort, uint16_t localPort);
    ~Client();
    void initSockAddr(const char* serverIPv6Addr, uint16_t serverPort);
    bool initUdpSockets(uint16_t remotePort, uint16_t localPort);
    int receiveUdpPacket();
    int forwardUdpPacket();
    void setKeepRunning(bool flag) { keepRunning = flag; }
    int sendUdpPacketToCtrl();
    int start();
    int stop();
private:
    SOCKET udpSokcetConnectWithLocalHost;
    vector<SOCKET> udpSokcetsConnectWithRemoteHost;
    sockaddr_in ctrlSockAddr;
    SOCKET ctrlSocket;
    vector<SOCKET> udpSockets;
    fd_set udpSokcetsFDSet;
    vector<sockaddr_in6> serverAddrVector;
    sockaddr_in clientAddr;
    queue<UdpPacketPtr> udpPacketQueue;
    mutex udpPacketQueueMutex;
    condition_variable udpPacketQueueCV;
    bool keepRunning;
};

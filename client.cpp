/*
 * File: client.cpp
 * Author: zzDragon
 * Created: 2024-07-01
 * Last Modified: 2024-07-01
 * Description: 
 *  1. Listen localhost:27015 to receive udp from localhost
 *  2. Forward localhost udp to remotehost:port[8905-8908]
 *  3. Listen to ports[8905-8908] to receive UDP from remotehost
 *  4. Forward remotehost udp to localhost
 */
#include <bits/stdc++.h>
#include <WS2tcpip.h>   // 包含Winsock2和网络相关的头文件

#pragma comment(lib, "Ws2_32.lib")  // 链接Ws2_32.lib库文件

using namespace std;
const uint32_t udp_packet_size = (1 << 16) - 1;
struct DataPacket
{
    sockaddr_in send_addr;
    SOCKET recv_sock;
    char* data;
    uint32_t dataBytes;
    ~DataPacket() {
        if (data != nullptr) {
            delete[] data;  // 释放动态分配的数据内存
        }
    }
};
using dataPacketPtr = shared_ptr<DataPacket>;
struct UdpSocket
{
    sockaddr_in udpSockAddr;
    SOCKET sock;
};

vector<UdpSocket> udpSocketsConnectWithRemoteHost;
fd_set readSetRH;
queue<dataPacketPtr> udpPacketFromRH;
mutex mutexUdpPacketFromRH;
vector<UdpSocket> udpSocketsConnectWithLocalHost;
fd_set readSetLH;
queue<dataPacketPtr> udpPacketFromLH;
mutex mutexUdpPacketFromLH;

map<string, SOCKET> mapIp2Socket;
map<SOCKET, string> mapSocket2Ip;
uint16_t client_port = 0;

bool running_flag = true;

SOCKET createListenSocket(sockaddr_in sockAddr)
{
    // create udp socket
    SOCKET sock = socket(sockAddr.sin_family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        cerr << __func__ << ": can't create socket! error code " << WSAGetLastError() << endl;
        running_flag = false;
        return SOCKET_ERROR;
    }
    // bind udp socket
    if (bind(sock, reinterpret_cast<sockaddr*>(&sockAddr), sizeof(sockAddr)) == SOCKET_ERROR) {
        cerr << __func__ << ": bind failed! error code " << WSAGetLastError() << endl;
        closesocket(sock);
        running_flag = false;
        return SOCKET_ERROR;
    }
    cout << "udp socket listenning from " << inet_ntoa(sockAddr.sin_addr)
            << ":" << ntohs(sockAddr.sin_port) << endl;
    return sock;
}

void udpRecvFromRemoteHost()
{
    // recv from remote host
    for (uint16_t i = 3460; i < 3460 + 4; ++ i)
    {
        // init sockaddr_in
        sockaddr_in sockAddr;
        // init sockaddr_in
        sockAddr.sin_family = AF_INET;
        sockAddr.sin_addr.s_addr = INADDR_ANY;
        sockAddr.sin_port = htons(i);
        // init SOCKET and add to udpSocketsConnectWithRemoteHost
        udpSocketsConnectWithRemoteHost.push_back({sockAddr, createListenSocket(sockAddr)});
    }

    // recv data
    while (running_flag)
    {
        if (udpSocketsConnectWithRemoteHost.size() == 0)
        {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        FD_ZERO(&readSetRH);
        for (int i = 0; i < udpSocketsConnectWithRemoteHost.size(); ++ i)
        {
            FD_SET(udpSocketsConnectWithRemoteHost[i].sock, &readSetRH);
        }
        if (select(0, &readSetRH, NULL, NULL, NULL) == SOCKET_ERROR) {
            std::cerr << "Select failed with error: " << WSAGetLastError() << "\n";
            break;
        }
        for (int i = 0; i < udpSocketsConnectWithRemoteHost.size(); ++ i)
        {
            if (FD_ISSET(udpSocketsConnectWithRemoteHost[i].sock, &readSetRH))
            {
                // init dataPacketPtr
                dataPacketPtr dpp = make_shared<DataPacket>();
                DataPacket* pdp = dpp.get();
                // send_addr, dataBytes, recv_sock
                char packet_buffer[udp_packet_size];
                int addrLen = sizeof(pdp->send_addr);
                pdp->dataBytes = recvfrom(
                    udpSocketsConnectWithRemoteHost[i].sock, packet_buffer, udp_packet_size, 0, 
                    reinterpret_cast<sockaddr*>(&pdp->send_addr), &addrLen
                );
                pdp->recv_sock = udpSocketsConnectWithRemoteHost[i].sock;
                // data
                if (pdp->dataBytes == SOCKET_ERROR)
                {
                    cerr << __func__ <<": recv failed! error code " << WSAGetLastError() << endl;
                }
                else
                {
                    pdp->data = new char[pdp->dataBytes];
                    memcpy(pdp->data, packet_buffer, pdp->dataBytes);
                    {
                        lock_guard<mutex> lock(mutexUdpPacketFromRH);
                        udpPacketFromRH.push(dpp);
                    }
                }
            }
        }
    }

    // close socket before exit current thread
    for (int i = 0; i < udpSocketsConnectWithRemoteHost.size(); ++i)
    {
        closesocket(udpSocketsConnectWithRemoteHost[i].sock);
    }
}

void udpRecvFromLocalHost()
{
    // recv from Local host
    {
        // init sockaddr_in
        sockaddr_in sockAddr;
        // init sockaddr_in
        sockAddr.sin_family = AF_INET;
        sockAddr.sin_addr.s_addr = INADDR_ANY;
        sockAddr.sin_port = htons(27015);
        // init SOCKET and add to udpSocketsConnectWithRemoteHost
        udpSocketsConnectWithLocalHost.push_back({sockAddr, createListenSocket(sockAddr)});
    }

    // recv data
    while (running_flag)
    {
        if (udpSocketsConnectWithLocalHost.size() == 0)
        {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        FD_ZERO(&readSetLH);
        for (int i = 0; i < udpSocketsConnectWithLocalHost.size(); ++ i)
        {
            FD_SET(udpSocketsConnectWithLocalHost[i].sock, &readSetLH);
        }
        if (select(0, &readSetLH, NULL, NULL, NULL) == SOCKET_ERROR) {
            std::cerr << "Select failed with error: " << WSAGetLastError() << "\n";
            break;
        }
        for (int i = 0; i < udpSocketsConnectWithLocalHost.size(); ++ i)
        {
            if (FD_ISSET(udpSocketsConnectWithLocalHost[i].sock, &readSetLH))
            {
                // init dataPacketPtr
                dataPacketPtr dpp = make_shared<DataPacket>();
                DataPacket* pdp = dpp.get();
                // send_addr, dataBytes, recv_sock
                char packet_buffer[udp_packet_size];
                int addrLen = sizeof(pdp->send_addr);
                pdp->dataBytes = recvfrom(
                    udpSocketsConnectWithLocalHost[i].sock, packet_buffer, udp_packet_size, 0, 
                    reinterpret_cast<sockaddr*>(&pdp->send_addr), &addrLen
                );
                pdp->recv_sock = udpSocketsConnectWithLocalHost[i].sock;
                // data
                if (pdp->dataBytes == SOCKET_ERROR)
                {
                    cerr << __func__ <<": recv failed! error code " << WSAGetLastError() << endl;
                }
                else
                {
                    pdp->data = new char[pdp->dataBytes];
                    memcpy(pdp->data, packet_buffer, pdp->dataBytes);
                    {
                        lock_guard<mutex> lock(mutexUdpPacketFromLH);
                        udpPacketFromLH.push(dpp);
                    }
                }
            }
        }
    }

    // close socket before exit current thread
    for (int i = 0; i < udpSocketsConnectWithLocalHost.size(); ++i)
    {
        closesocket(udpSocketsConnectWithLocalHost[i].sock);
    }
}

void udpSendToLocalHost()               // one ip map one socket, no map then create one
{
    // handle recved data packet
    while (running_flag)
    {
        if (udpPacketFromRH.empty())
        {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        else
        {
            dataPacketPtr firstElement;
            {
                lock_guard<mutex> lock(mutexUdpPacketFromRH);
                firstElement = udpPacketFromRH.front();
                udpPacketFromRH.pop();
            }
            if (client_port == 0)
            {
                cerr << "I don't know who to send it to" << endl;
                continue;
            }
            DataPacket* pdp = firstElement.get();
            sockaddr_in send_addr;
            send_addr.sin_family = AF_INET;
            send_addr.sin_port = htons(client_port);
            send_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            SOCKET sendSocket;
            auto found = mapIp2Socket.find(inet_ntoa(pdp->send_addr.sin_addr));
            if (found != mapIp2Socket.end())
            {
                sendSocket = found->second;
            }
            else
            {
                sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                mapIp2Socket[inet_ntoa(pdp->send_addr.sin_addr)] = sendSocket;
                mapSocket2Ip[sendSocket] = inet_ntoa(pdp->send_addr.sin_addr);
            }
            int sendResult = sendto(sendSocket, pdp->data, pdp->dataBytes, 0, 
                reinterpret_cast<sockaddr*>(&send_addr), sizeof(send_addr));
            if (sendResult == -1)
            {
                cerr << "sendto failed " << WSAGetLastError() << pdp->dataBytes << endl;
            }
            else
            {
                cout << "Sent " << sendResult << " bytes to " << inet_ntoa(send_addr.sin_addr) << ":" << ntohs(send_addr.sin_port) << endl;
            }
        }
    }
}

void udpSendToRemoteHost()              // one socket map one ip, no map then continue, only for server endium
{
    // handle recved data packet
    while (running_flag)
    {
        if (udpPacketFromLH.empty())
        {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        else
        {
            dataPacketPtr firstElement;
            {
                lock_guard<mutex> lock(mutexUdpPacketFromLH);
                firstElement = udpPacketFromLH.front();
                udpPacketFromLH.pop();
            }
            DataPacket* pdp = firstElement.get();
            sockaddr_in send_addr;
            send_addr.sin_family = AF_INET;
            send_addr.sin_port = htons(8905);
            send_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            int sendResult = sendto(udpSocketsConnectWithRemoteHost[time(nullptr) / 60 % 60 % udpSocketsConnectWithRemoteHost.size()].sock, pdp->data, pdp->dataBytes, 0, 
                reinterpret_cast<sockaddr*>(&send_addr), sizeof(send_addr));
            if (sendResult == -1)
            {
                cerr << "sendto failed " << WSAGetLastError() << pdp->dataBytes << endl;
            }
            else
            {
                cout << "Sent " << sendResult << " bytes to " << inet_ntoa(send_addr.sin_addr) << ":" << ntohs(send_addr.sin_port) << endl;
            }
        }
    }
}

int main() {
    // 初始化Winsock
    WSADATA data;
    WORD version = MAKEWORD(2, 2);
    int wsOK = WSAStartup(version, &data);
    if (wsOK != 0) {
        cerr << __func__ <<": can't init socket" << wsOK << endl;
        return -1;
    }

    // init all thread
    vector<thread> threadPool;
    threadPool.push_back(thread(udpRecvFromRemoteHost));
    threadPool.push_back(thread(udpRecvFromLocalHost));
    threadPool.push_back(thread(udpSendToLocalHost));
    threadPool.push_back(thread(udpSendToRemoteHost));

    // wait these thread exit
    for_each(threadPool.begin(), threadPool.end(), [](thread& t)
    {
        if (t.joinable())
        {
            t.join();
        }
    });    
    
    WSACleanup();

    return 0;
}

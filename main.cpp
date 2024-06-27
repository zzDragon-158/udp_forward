#include <bits/stdc++.h>
#include <thread>
#include <WS2tcpip.h>   // 包含Winsock2和网络相关的头文件

#pragma comment(lib, "Ws2_32.lib")  // 链接Ws2_32.lib库文件

using namespace std;
const uint32_t udp_packet_size = 1 << (16 - 1);
struct DataPacket
{
    sockaddr_in send_addr;
    sockaddr_in recv_addr;
    char* data;
    uint32_t dataBytes;
    ~DataPacket() {
        if (data != nullptr) {
            delete[] data;  // 释放动态分配的数据内存
        }
    }
};
using dataPacketPtr = shared_ptr<DataPacket>;
queue<dataPacketPtr> packet_queue;
mutex packetQueueMutex;
bool running_flag = true;

void udpRecv(SOCKET& sock, sockaddr_in sock_addr)
{
    // recv data
    while (running_flag)
    {
        // init dataPacketPtr
        dataPacketPtr dpp = make_shared<DataPacket>();
        DataPacket* pdp = dpp.get();
        // send_addr, dataBytes, recv_addr
        char packet_buffer[udp_packet_size];
        int addrLen = sizeof(pdp->send_addr);
        pdp->dataBytes = recvfrom(
            sock, packet_buffer, udp_packet_size, 0, 
            reinterpret_cast<sockaddr*>(&pdp->send_addr), &addrLen
        );
        pdp->recv_addr = sock_addr;
        // data
        if (pdp->dataBytes == SOCKET_ERROR) {
            cerr << __func__ <<": recv failed! error code " << WSAGetLastError() << endl;
        } else {
            pdp->data = new char[pdp->dataBytes];
            memcpy(pdp->data, packet_buffer, pdp->dataBytes);
            {
                lock_guard<mutex> lock(packetQueueMutex);
                packet_queue.push(dpp);
            }
        }
    }
    // free memory before exit this thread
    // todo
    closesocket(sock);
}

bool createSocket(sockaddr_in sock_addr, SOCKET& sock)
{
    // create udp socket
    sock = socket(sock_addr.sin_family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        cerr << __func__ << ": can't create socket! error code " << WSAGetLastError() << endl;
        running_flag = false;
        return false;
    }
    // bind udp socket
    if (bind(sock, reinterpret_cast<sockaddr*>(&sock_addr), sizeof(sock_addr)) == SOCKET_ERROR) {
        cerr << __func__ << ": bind failed! error code " << WSAGetLastError() << endl;
        closesocket(sock);
        running_flag = false;
        return false;
    }
    return true;
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
    
    // init udp socket
    sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    // recv from local host
    sock_addr.sin_port = htons(3460);
    sock_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    SOCKET udpRecvFromLocalHostSocket;
    createSocket(sock_addr, udpRecvFromLocalHostSocket);
    cout << "udp socket listenning from " << inet_ntoa(sock_addr.sin_addr)
                << ":" << ntohs(sock_addr.sin_port) << endl;
    thread udpRecvFromLocalHost(udpRecv, ref(udpRecvFromLocalHostSocket), sock_addr);

    // recv from remote host
    sock_addr.sin_port = htons(8905);
    sock_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    SOCKET udpRecvFromRemoteHostSocket;
    createSocket(sock_addr, udpRecvFromRemoteHostSocket);
    cout << "udp socket listenning from " << inet_ntoa(sock_addr.sin_addr)
            << ":" << ntohs(sock_addr.sin_port) << endl;
    thread udpRecvFromRemoteHost(udpRecv, ref(udpRecvFromRemoteHostSocket), sock_addr);

    // handle recved data packet
    while (running_flag)
    {
        if (packet_queue.empty())
        {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        else
        {
            dataPacketPtr firstElement;
            {
                lock_guard<mutex> lock(packetQueueMutex);
                firstElement = packet_queue.front();
                packet_queue.pop();
            }
            DataPacket* pdp = firstElement.get();
            SOCKET* pSendSocket;
            sockaddr_in send_addr;
            send_addr.sin_family = AF_INET;
            send_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            if (ntohs(pdp->recv_addr.sin_port) == 3460)
            {
                pSendSocket = &udpRecvFromRemoteHostSocket;
                send_addr.sin_port = htons(8906);
            }
            else if (ntohs(pdp->send_addr.sin_port) == 8905)
            {
                pSendSocket = &udpRecvFromLocalHostSocket;
                send_addr.sin_port = htons(3461);
            }
            int sendResult = sendto(*pSendSocket, pdp->data, pdp->dataBytes, 0, 
                reinterpret_cast<sockaddr*>(&send_addr), sizeof(send_addr));
            if (sendResult == -1)
            {
                cerr << "sendto failed " << WSAGetLastError() << pdp->dataBytes << endl;
            }
            else
            {
                cout << "Sent " << sendResult << " bytes to " << inet_ntoa(pdp->send_addr.sin_addr) << ":" << ntohs(pdp->send_addr.sin_port) << endl;
            }
        }
    }
    WSACleanup();

    return 0;
}

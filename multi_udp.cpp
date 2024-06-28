#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

#define MAX_SOCKETS 10

int main() {
    WSADATA wsaData;
    SOCKET udpSockets[MAX_SOCKETS];
    struct sockaddr_in serverAddr;
    fd_set readSet;
    int numSockets = 2; // Example: Listen on two sockets

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    // Create UDP sockets
    for (int i = 0; i < numSockets; ++i) {
        udpSockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSockets[i] == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
            WSACleanup();
            return 1;
        }

        // Bind the socket to any IP address and a specific port
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(5000 + i); // Example: Port 5000, 5001, ...
        
        if (bind(udpSockets[i], (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed with error: " << WSAGetLastError() << "\n";
            closesocket(udpSockets[i]);
            WSACleanup();
            return 1;
        }
    }

    // Main loop to listen for incoming data
    while (true) {
        FD_ZERO(&readSet);
        for (int i = 0; i < numSockets; ++i) {
            FD_SET(udpSockets[i], &readSet);
        }

        // Use select to wait for activity on any of the sockets
        int activity = select(0, &readSet, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            std::cerr << "Select failed with error: " << WSAGetLastError() << "\n";
            break;
        }

        // Check each socket for incoming data
        for (int i = 0; i < numSockets; ++i) {
            if (FD_ISSET(udpSockets[i], &readSet)) {
                char buffer[1024];
                struct sockaddr_in clientAddr;
                int clientAddrLen = sizeof(clientAddr);
                int recvLen = recvfrom(udpSockets[i], buffer, sizeof(buffer), 0,
                                       (struct sockaddr *)&clientAddr, &clientAddrLen);
                if (recvLen > 0) {
                    buffer[recvLen] = '\0';
                    std::cout << "Received data on socket " << i << ": " << buffer << "\n";
                } else if (recvLen == 0) {
                    std::cout << "Connection closed\n";
                } else {
                    std::cerr << "recvfrom failed with error: " << WSAGetLastError() << "\n";
                }
            }
        }
    }

    // Cleanup
    for (int i = 0; i < numSockets; ++i) {
        closesocket(udpSockets[i]);
    }
    WSACleanup();

    return 0;
}

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <IlmeeeEditor.h>
#include <string>
#include <iostream>
using namespace std;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wsock32.lib")
namespace IlmeeeEditor {
class TCPConnection {
public:
    static const int PORT = 27015;
    
    TCPConnection() : socket_(INVALID_SOCKET), isConnected_(false) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    ~TCPConnection() {
        disconnect();
        WSACleanup();
    }

    bool startServer() {
        SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) return false;

        sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(PORT);

        if (bind(listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(listenSocket);
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listenSocket);
            return false;
        }

        socket_ = accept(listenSocket, NULL, NULL);
        closesocket(listenSocket);
        
        if (socket_ == INVALID_SOCKET) return false;
        
        isConnected_ = true;
        return true;
    }

    bool connectToServer() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) return false;

        sockaddr_in clientService;
        clientService.sin_family = AF_INET;
        InetPton(AF_INET, "127.0.0.1", &clientService.sin_addr.s_addr);
        clientService.sin_port = htons(PORT);

        if (connect(socket_, (SOCKADDR*)&clientService, sizeof(clientService)) == SOCKET_ERROR) {
            closesocket(socket_);
            return false;
        }
        cout << "Connected To Server: " << inet_ntoa(clientService.sin_addr) << ":" << ntohs(clientService.sin_port) << endl;
        isConnected_ = true;
        return true;
    }

    bool sendMessage(const std::string& message) {
        if (!isConnected_) return false;
        return send(socket_, message.c_str(), message.length(), 0) != SOCKET_ERROR;
    }

    std::string receiveMessage() {
        if (!isConnected_) return "";
        
        char buffer[1024];
        int result = recv(socket_, buffer, sizeof(buffer), 0);
        
        if (result > 0) {
            return std::string(buffer, result);
        }
        return "";
    }

private:
    void disconnect() {
        if (isConnected_) {
            closesocket(socket_);
            isConnected_ = false;
        }
    }

    SOCKET socket_;
    bool isConnected_;
};
} // namespace IlmeeeEditor
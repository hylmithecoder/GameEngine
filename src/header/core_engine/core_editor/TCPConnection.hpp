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
    static const int PORTCore = 27016;
    
    TCPConnection() : socket_(INVALID_SOCKET), isConnected_(false) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    ~TCPConnection() {
        disconnectFromEngine();
        WSACleanup();
    }

    bool startServerCore() {
        cout << "Starting server core..." << endl;
        socketCore_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socketCore_ == INVALID_SOCKET)
        {
            cout << "Failed to create server socket" << endl;
            return false;
        }

        sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(PORTCore);

        // if (bind(socketCore_, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        //     closesocket(socketCore_);
        //     cout << "Bind failed" << endl;
        //     return false;
        // }

        // if (listen(socketCore_, SOMAXCONN) == SOCKET_ERROR) {
        //     closesocket(socketCore_);
        //     cout << "Listen failed" << endl;
        //     return false;
        // }

        // Enable socket reuse
        int opt = 1;
        if (setsockopt(socketCore_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            cout << "setsockopt failed" << endl;
            closesocket(socketCore_);
            return false;
        }

        if (bind(socketCore_, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            cout << "Bind failed" << endl;
            closesocket(socketCore_);
            return false;
        }

        if (listen(socketCore_, SOMAXCONN) == SOCKET_ERROR) {
            cout << "Listen failed" << endl;
            closesocket(socketCore_);
            return false;
        }

        // socketCore_ = accept(socketCore_, NULL, NULL);
        // closesocket(socketCore_);
        isConnected_ = true;
        
        // if (socketCore_ == INVALID_SOCKET) return false;
        
        // Start listener thread
        listenThread_ = std::thread(&TCPConnection::listenForConnectionsEngine, this);
        cout << "Server dll started on port " << PORTCore << " and socket " << socketCore_ << endl;

        return true;
    }

    bool connectToEngine() {
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
        cout << "Connected To Server: " << inet_ntoa(clientService.sin_addr) << ":" << ntohs(clientService.sin_port) << " Family: " << clientService.sin_family << endl;
        isConnected_ = true;
        return true;
    }

    bool sendMessageToEngine(const std::string& message) {
        if (!isConnected_) return false;

        std::lock_guard<std::mutex> lock(sendMutex_);
        int bytesSent = send(socket_, message.c_str(), message.length(), 0);
        if (bytesSent == SOCKET_ERROR) {
            cout << "Send failed: " << WSAGetLastError() << endl;
            return false;
        }

        cout << "Sent to: " << socket_  << " Bytes Sent: " << bytesSent << " And Message: " << message << endl;
        return true;
    }

    std::string receiveMessageFromEngine() {
        // if (!isConnected_) return "";
        
        // char buffer[1024];
        // int result = recv(socket_, buffer, sizeof(buffer), 0);
        
        // if (result > 0) {
        //     return std::string(buffer, result);
        // }
        // return "";
        std::lock_guard<std::mutex> lock(receiveMutex_);
        if (!messageQueue_.empty()) {
            std::string msg = messageQueue_.front();
            messageQueue_.pop();
            cout << "Menerima Pesan Dari: " << socketCore_ << " Yang Berisi: " << msg << endl;
            return msg;
        }
        return "";
    }

private:
    void disconnectFromEngine() {
        if (isConnected_) {
            closesocket(socket_);
            isConnected_ = false;
        }
    }

    void listenForConnectionsEngine() {
        while (isConnected_) {
            SOCKET serverSocket = accept(socketCore_, NULL, NULL);
            if (serverSocket != INVALID_SOCKET) {
                std::lock_guard<std::mutex> lock(receiveMutex_);
                serverSockets_.push_back(serverSocket);
                
                // Start a new thread to handle this client
                std::thread(&TCPConnection::handleClientCore, this, serverSocket).detach();
                cout << "New client connected" << endl;
            }
        }
    }

    void handleClientCore(SOCKET clientSocket) {
        char buffer[1024];
        while (isConnected_) {
            int bytesReceived = recv(clientSocket, buffer, 1024 - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::lock_guard<std::mutex> lock(receiveMutex_);
                messageQueue_.push(std::string(buffer, bytesReceived));
                cout << "Received: " << buffer << endl;
            }
            else if (bytesReceived == 0) {
                cout << "Client disconnected" << endl;
                break;
            }
            else {
                cout << "Receive failed" << endl;
                break;
            }
        }
        closesocket(clientSocket);
    }

    SOCKET socket_;
    SOCKET socketCore_;
    bool isConnected_;
    mutex receiveMutex_;
    mutex sendMutex_;
    vector<SOCKET> serverSockets_;
    queue<std::string> messageQueue_;
    thread listenThread_;
};
} // namespace IlmeeeEditor
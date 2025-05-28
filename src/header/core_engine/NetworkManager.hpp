#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

class NetworkManager {
public:
    static const int PORT = 27015;

    NetworkManager() : socket_(INVALID_SOCKET), isRunning_(false) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~NetworkManager() {
        stop();
        WSACleanup();
    }

    bool startServer() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) return false;

        sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(PORT);

        if (bind(socket_, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(socket_);
            return false;
        }

        if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(socket_);
            return false;
        }

        isRunning_ = true;
        listenThread_ = std::thread(&NetworkManager::listenForConnections, this);
        return true;
    }

    bool sendMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        return send(socket_, message.c_str(), message.length(), 0) != SOCKET_ERROR;
    }

    std::string receiveMessage() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!messageQueue_.empty()) {
            std::string msg = messageQueue_.front();
            messageQueue_.pop();
            return msg;
        }
        return "";
    }
    
    void stop() {
        isRunning_ = false;
        if (listenThread_.joinable()) {
            listenThread_.join();
        }
        for (auto& socket : clientSockets_) {
            closesocket(socket);
        }
        closesocket(socket_);
    }

private:
    void listenForConnections() {
        while (isRunning_) {
            SOCKET clientSocket = accept(socket_, NULL, NULL);
            if (clientSocket != INVALID_SOCKET) {
                clientSockets_.push_back(clientSocket);
                std::thread(&NetworkManager::handleClient, this, clientSocket).detach();
            }
        }
    }

    void handleClient(SOCKET clientSocket) {
        char buffer[1024];
        while (isRunning_) {
            int result = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (result > 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                messageQueue_.push(std::string(buffer, result));
            }
            else break;
        }
        closesocket(clientSocket);
    }

    SOCKET socket_;
    bool isRunning_;
    std::thread listenThread_;
    std::vector<SOCKET> clientSockets_;
    std::queue<std::string> messageQueue_;
    std::mutex mutex_;
};
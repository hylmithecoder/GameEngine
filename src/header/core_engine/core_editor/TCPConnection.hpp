#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>        // close
#include <errno.h>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <iostream>

namespace IlmeeeEditor {

class TCPConnection {
public:
    static const int PORT = 27015;
    static const int PORTCore = 27016;

    TCPConnection() : socket_(-1), socketCore_(-1), isConnected_(false) {}

    ~TCPConnection() {
        disconnectFromEngine();
    }

    bool startServerCore() {
        std::cout << "Starting server core..." << std::endl;
        socketCore_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socketCore_ < 0) {
            perror("Socket creation failed");
            return false;
        }

        int opt = 1;
        if (setsockopt(socketCore_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt failed");
            close(socketCore_);
            return false;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(PORTCore);

        if (bind(socketCore_, (sockaddr*)&service, sizeof(service)) < 0) {
            perror("Bind failed");
            close(socketCore_);
            return false;
        }

        if (listen(socketCore_, SOMAXCONN) < 0) {
            perror("Listen failed");
            close(socketCore_);
            return false;
        }

        isConnected_ = true;
        listenThread_ = std::thread(&TCPConnection::listenForConnectionsEngine, this);
        std::cout << "Server core started on port " << PORTCore << std::endl;
        return true;
    }

    bool connectToEngine() {
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            perror("Socket creation failed");
            return false;
        }

        sockaddr_in clientService{};
        clientService.sin_family = AF_INET;
        clientService.sin_port = htons(PORT);
        inet_pton(AF_INET, "127.0.0.1", &clientService.sin_addr);

        if (connect(socket_, (sockaddr*)&clientService, sizeof(clientService)) < 0) {
            perror("Connect failed");
            close(socket_);
            return false;
        }

        std::cout << "Connected to engine!" << std::endl;
        isConnected_ = true;
        return true;
    }

    bool sendMessageToEngine(const std::string& message) {
        if (!isConnected_) return false;
        std::lock_guard<std::mutex> lock(sendMutex_);
        ssize_t bytesSent = send(socket_, message.c_str(), message.length(), 0);
        if (bytesSent < 0) {
            perror("Send failed");
            return false;
        }
        std::cout << "Sent: " << message << std::endl;
        return true;
    }

    std::string receiveMessageFromEngine() {
        std::lock_guard<std::mutex> lock(receiveMutex_);
        if (!messageQueue_.empty()) {
            std::string msg = messageQueue_.front();
            messageQueue_.pop();
            std::cout << "Received from queue: " << msg << std::endl;
            return msg;
        }
        return "";
    }

private:
    void disconnectFromEngine() {
        if (isConnected_) {
            if (socket_ >= 0) close(socket_);
            isConnected_ = false;
        }
    }

    void listenForConnectionsEngine() {
        while (isConnected_) {
            int clientSocket = accept(socketCore_, nullptr, nullptr);
            if (clientSocket >= 0) {
                std::lock_guard<std::mutex> lock(receiveMutex_);
                serverSockets_.push_back(clientSocket);
                std::thread(&TCPConnection::handleClientCore, this, clientSocket).detach();
                std::cout << "New client connected" << std::endl;
            }
        }
    }

    void handleClientCore(int clientSocket) {
        char buffer[1024];
        while (isConnected_) {
            ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::lock_guard<std::mutex> lock(receiveMutex_);
                messageQueue_.push(std::string(buffer));
                std::cout << "Received: " << buffer << std::endl;
            } else {
                break;
            }
        }
        close(clientSocket);
    }

    int socket_;
    int socketCore_;
    bool isConnected_;
    std::mutex receiveMutex_;
    std::mutex sendMutex_;
    std::vector<int> serverSockets_;
    std::queue<std::string> messageQueue_;
    std::thread listenThread_;
};

} // namespace IlmeeeEditor

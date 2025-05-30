#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <Debugger.hpp>
#pragma comment(lib, "ws2_32.lib")

class NetworkManager {
public:
    static const int PORT = 27015;
    static const int BUFFER_SIZE = 1024;

    NetworkManager() : 
        socket_(INVALID_SOCKET), 
        isRunning_(false),
        isConnected_(false) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    bool startServer() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            Debug::Logger::Log("Failed to create server socket", Debug::LogLevel::CRASH);
            return false;
        }

        sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(PORT);

        // Enable socket reuse
        int opt = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            Debug::Logger::Log("setsockopt failed", Debug::LogLevel::CRASH);
            closesocket(socket_);
            return false;
        }

        if (bind(socket_, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            Debug::Logger::Log("Bind failed", Debug::LogLevel::CRASH);
            closesocket(socket_);
            return false;
        }

        if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) {
            Debug::Logger::Log("Listen failed", Debug::LogLevel::CRASH);
            closesocket(socket_);
            return false;
        }

        isRunning_ = true;
        isConnected_ = true;
        
        // Start listener thread
        listenThread_ = std::thread(&NetworkManager::listenForConnections, this);
        Debug::Logger::Log("Server started on port " + std::to_string(PORT), Debug::LogLevel::SUCCESS);
        
        return true;
    }

    bool connectToServer() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            Debug::Logger::Log("Failed to create client socket", Debug::LogLevel::CRASH);
            return false;
        }

        sockaddr_in clientService;
        clientService.sin_family = AF_INET;
        InetPton(AF_INET, "127.0.0.1", &clientService.sin_addr.s_addr);
        clientService.sin_port = htons(PORT);

        if (connect(socket_, (SOCKADDR*)&clientService, sizeof(clientService)) == SOCKET_ERROR) {
            Debug::Logger::Log("Connection failed", Debug::LogLevel::CRASH);
            closesocket(socket_);
            return false;
        }

        isConnected_ = true;
        isRunning_ = true;

        // Start receive thread for client
        receiveThread_ = std::thread(&NetworkManager::handleMessages, this);
        Debug::Logger::Log("Connected to server", Debug::LogLevel::SUCCESS);
        
        return true;
    }

    bool sendMessage(const std::string& message) {
        if (!isConnected_) return false;

        std::lock_guard<std::mutex> lock(sendMutex_);
        int bytesSent = send(socket_, message.c_str(), message.length(), 0);
        // Debug::Logger::Log("[IlmeeeEngine] Sending: " + message, Debug::LogLevel::INFO);
        if (bytesSent == SOCKET_ERROR) {
            Debug::Logger::Log("Send failed: " + std::to_string(WSAGetLastError()), Debug::LogLevel::CRASH);
            return false;
        }
        
        Debug::Logger::Log("Sent: " + message, Debug::LogLevel::SUCCESS);
        return true;
    }

    std::string receiveMessage() {
        std::lock_guard<std::mutex> lock(receiveMutex_);
        if (!messageQueue_.empty()) {
            std::string msg = messageQueue_.front();
            messageQueue_.pop();
            return msg;
        }
        return "";
    }

    void stop() {
        isRunning_ = false;
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
        if (listenThread_.joinable()) {
            listenThread_.join();
        }
        if (receiveThread_.joinable()) {
            receiveThread_.join();
        }
    }
private:
    void listenForConnections() {
        while (isRunning_) {
            SOCKET clientSocket = accept(socket_, NULL, NULL);
            if (clientSocket != INVALID_SOCKET) {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clientSockets_.push_back(clientSocket);
                
                // Start a new thread to handle this client
                std::thread(&NetworkManager::handleClient, this, clientSocket).detach();
                Debug::Logger::Log("New client connected", Debug::LogLevel::INFO);
            }
        }
    }

    void handleClient(SOCKET clientSocket) {
        char buffer[BUFFER_SIZE];
        while (isRunning_) {
            int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::lock_guard<std::mutex> lock(receiveMutex_);
                messageQueue_.push(std::string(buffer, bytesReceived));
                Debug::Logger::Log("Received: " + std::string(buffer), Debug::LogLevel::INFO);
            }
            else if (bytesReceived == 0) {
                Debug::Logger::Log("Client disconnected", Debug::LogLevel::WARNING);
                break;
            }
            else {
                Debug::Logger::Log("Receive failed", Debug::LogLevel::CRASH);
                break;
            }
        }
        closesocket(clientSocket);
    }

    void handleMessages() {
        char buffer[BUFFER_SIZE];
        while (isRunning_) {
            int bytesReceived = recv(socket_, buffer, BUFFER_SIZE - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::lock_guard<std::mutex> lock(receiveMutex_);
                messageQueue_.push(std::string(buffer, bytesReceived));
                Debug::Logger::Log("Received: " + std::string(buffer), Debug::LogLevel::INFO);
            }
        }
    }

    SOCKET socket_;
    bool isRunning_;
    bool isConnected_;
    std::thread listenThread_;
    std::thread receiveThread_;
    std::vector<SOCKET> clientSockets_;
    std::queue<std::string> messageQueue_;
    std::mutex sendMutex_;
    std::mutex receiveMutex_;
    std::mutex clientsMutex_;
};
#pragma once
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

    
class TCPClient {
private:
    int sockfd;
    struct sockaddr_in serverAddr;

public:
    TCPClient() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Error creating socket." << std::endl;
            exit(1);
        }
    }

    void connectToServer(const char* ip, int port) {
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &serverAddr.sin_addr);

        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Connection failed." << std::endl;
            exit(1);
        }
        // std::cout << "Connected to server." << std::endl;
    }

    void sendMsg(const std::string& msg) {
        send(sockfd, msg.c_str(), msg.length(), 0);
    }

    std::string receiveMsg() {
        char buffer[1024] = {0};
        int valread = read(sockfd, buffer, 1024);
        return std::string(buffer, valread);
    }

    void disconnect() {
        close(sockfd);
        // std::cout << "Disconnected from server." << std::endl;
    }

    ~TCPClient() {
        close(sockfd);
    }
};

class TCPServer {
private:
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

public:
    TCPServer(int port) {
        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            std::cerr << "Socket creation failed." << std::endl;
            exit(1);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
            std::cerr << "Bind failed." << std::endl;
            exit(1);
        }

        if (listen(server_fd, 3) < 0) {
            std::cerr << "Listen failed." << std::endl;
            exit(1);
        }
    }

    void acceptConnection() {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
            std::cerr << "Accept failed." << std::endl;
            exit(1);
        }
        // std::cout << "Connection accepted." << std::endl;
    }

    void sendMsg(const std::string &msg) {
        send(new_socket, msg.c_str(), msg.length(), 0);
    }

    std::string receiveMsg() {
        char buffer[1024] = {0};
        int valread = read(new_socket, buffer, 1024);
        return std::string(buffer, valread);
    }

    void disconnect() {
        close(new_socket);
        // std::cout << "Client disconnected." << std::endl;
    }

    ~TCPServer() {
        close(server_fd);
    }
};

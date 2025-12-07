#include "../include/tools/web_interface/web_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>
#include <iomanip>

WebServer::WebServer(int port) : port_(port), server_fd_(-1), running_(false) {
}

WebServer::~WebServer() {
    stop();
}

void WebServer::start() {
    if (running_) {
        return;
    }

    running_ = true;
    server_thread_ = std::thread(&WebServer::server_loop, this);
    write_log("WebServer: Started on port " + std::to_string(port_));
}

void WebServer::stop() {
    if (!running_) {
        return;
    }

    write_log("WebServer: Stopping...");
    running_ = false;

    // Close socket to interrupt accept() if it's blocking
    if (server_fd_ != -1) {
        int fd = server_fd_;
        server_fd_ = -1;
        close(fd);
    }

    // Wait for thread to finish with timeout
    if (server_thread_.joinable()) {
        // Give thread up to 2 seconds to finish
        auto start = std::chrono::system_clock::now();
        while (server_thread_.joinable()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::system_clock::now() - start;
            if (elapsed > std::chrono::seconds(2)) {
                write_log("WebServer: WARNING - Thread did not stop gracefully");
                break;
            }
        }
        if (server_thread_.joinable()) {
            server_thread_.detach();
        }
    }

    write_log("WebServer: Stopped");
}

void WebServer::server_loop() {
    write_log("WebServer: Creating socket...");

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        write_log("WebServer: ERROR - Failed to create socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        write_log("WebServer: ERROR - Failed to bind on port " + std::to_string(port_));
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    if (listen(server_fd_, 10) < 0) {
        write_log("WebServer: ERROR - Failed to listen");
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // Set SO_REUSEADDR to allow quick restart
    int reuse_val = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_val, sizeof(reuse_val));

    // Set socket to non-blocking mode for accept() timeout
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    write_log("WebServer: Listening on 0.0.0.0:" + std::to_string(port_));

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking mode, no client available yet
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (running_) {
                continue;
            }
            break;
        }

        // Handle client in separate thread
        std::thread([this, client_fd]() {
            handle_client(client_fd);
            close(client_fd);
        }).detach();
    }

    write_log("WebServer: Accept loop ended");
}

void WebServer::handle_client(int client_fd) {
    char buffer[4096] = {0};
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        return;
    }

    std::string request(buffer, bytes_read);
    std::string response = process_http_request(request);

    send(client_fd, response.c_str(), response.length(), 0);
}

std::string WebServer::process_http_request(const std::string& request) {
    std::istringstream request_stream(request);
    std::string method, path, version;
    request_stream >> method >> path >> version;

    write_log("WebServer: HTTP " + method + " " + path);

    if (method == "GET") {
        // Call handler if available
        if (get_handler_) {
            return get_handler_(path);
        }
        return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
    } else if (method == "POST") {
        // Extract body from request
        size_t body_start = request.find("\r\n\r\n");
        std::string body;
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
        write_log("WebServer: POST body size: " + std::to_string(body.length()));

        // Call handler if available
        if (post_handler_) {
            return post_handler_(path, body);
        }
        return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found";
    }

    return "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nMethod Not Allowed";
}

std::string WebServer::handle_get_request(const std::string& path) {
    return "";
}

std::string WebServer::handle_post_request(const std::string& path, const std::string& body) {
    return "";
}

std::string WebServer::serve_static_file(const std::string& file_path) {
    return "";
}

std::string WebServer::read_template(const std::string& template_name) {
    return "";
}

void WebServer::write_log(const std::string& message) {
    std::string timestamp;
    {
        time_t now = std::time(nullptr);
        struct tm* timeinfo = std::localtime(&now);
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        timestamp = buffer;
    }

    std::string log_message = "[" + timestamp + "] [WebServer] " + message;

    // Log to console
    std::cout << log_message << std::endl;

    // Log to file
    std::ofstream log_file("/var/log/refrigeration-api.log", std::ios::app);
    if (log_file.is_open()) {
        log_file << log_message << std::endl;
        log_file.close();
    }
}

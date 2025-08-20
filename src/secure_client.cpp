#include "secure_client.h"
#include "refrigeration.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <iostream>

SecureClient::SecureClient(const std::string& server_ip,
                           int port, const std::string& cert_file, const std::string& key_file, const std::string& ca_file)
    : server_ip_(server_ip), port_(port), cert_file_(cert_file), key_file_(key_file), ca_file_(ca_file),
      reconnect_delay_(5),  socket_fd_(-1),
      ctx_(nullptr), ssl_(nullptr)
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ctx_ = SSL_CTX_new(TLS_client_method());

        // Load CA (trusted certificates)
    if (!SSL_CTX_load_verify_locations(ctx_, ca_file_.c_str(), nullptr)) {
        logger.log_events("Error", "Failed to load CA file: " + ca_file_);
        ERR_print_errors_fp(stderr);
    }

    // Load client certificate and key
    if (!SSL_CTX_use_certificate_file(ctx_, cert_file_.c_str(), SSL_FILETYPE_PEM)) {
        logger.log_events("Error", "Failed to load client certificate");
        ERR_print_errors_fp(stderr);
    }
    if (!SSL_CTX_use_PrivateKey_file(ctx_, key_file_.c_str(), SSL_FILETYPE_PEM)) {
        logger.log_events("Error", "Failed to load client key");
        ERR_print_errors_fp(stderr);
    }

    // Require verification
    SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_verify_depth(ctx_, 4);
}

void SecureClient::connect() {
    cleanup();

    logger.log_events("Debug", "Attempting to connect to " + server_ip_ + ":" + std::to_string(port_));

    struct sockaddr_in server_addr{};
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        logger.log_events("Error", "Socket creation failed");
        return;
    }

    // Set socket to non-blocking
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr);

    int result = ::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0) {
        if (errno == EINPROGRESS) {
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(socket_fd_, &writefds);
            struct timeval tv;
            tv.tv_sec = 5; // 5 seconds timeout
            tv.tv_usec = 0;

            int sel = select(socket_fd_ + 1, nullptr, &writefds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(socket_fd_, &writefds)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error != 0) {
                    logger.log_events("Error", "Connection failed (SO_ERROR). Retrying in " + std::to_string(reconnect_delay_) + " seconds...");
                    close(socket_fd_);
                    std::this_thread::sleep_for(std::chrono::seconds(reconnect_delay_));
                    return;
                }
            } else {
                logger.log_events("Error", "Connection timed out. Retrying in " + std::to_string(reconnect_delay_) + " seconds...");
                close(socket_fd_);
                std::this_thread::sleep_for(std::chrono::seconds(reconnect_delay_));
                return;
            }
        } else {
            logger.log_events("Error", "Connection failed. Retrying in " + std::to_string(reconnect_delay_) + " seconds...");
            close(socket_fd_);
            std::this_thread::sleep_for(std::chrono::seconds(reconnect_delay_));
            return;
        }
    }

    // Restore socket to blocking mode
    fcntl(socket_fd_, F_SETFL, flags);

    ssl_ = SSL_new(ctx_);
    SSL_set_fd(ssl_, socket_fd_);

    if (SSL_connect(ssl_) <= 0) {
        logger.log_events("Error", "SSL connection failed");
        cleanup();
        return;
    }

    logger.log_events("Debug", "Securely connected to " + server_ip_ + ":" + std::to_string(port_));
}

nlohmann::json SecureClient::send_and_receive(const nlohmann::json& data_to_send) {
    if (!ssl_) connect();
    if (!ssl_) return nullptr;

    try {
        std::string json_data = data_to_send.dump();
        int sent = SSL_write(ssl_, json_data.c_str(), json_data.length());
        if (sent <= 0) throw std::runtime_error("Send failed");

        logger.log_events("Debug", "Sent: " + json_data);

        char buffer[1024] = {0};
        int received = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
        if (received <= 0) throw std::runtime_error("No data received");

        std::string received_str(buffer, received);
        logger.log_events("Debug", "Received: " + received_str);

        return nlohmann::json::parse(received_str);
    }
    catch (const std::exception& e) {
        logger.log_events("Error", "Communication error: " + std::string(e.what()));
        cleanup();
        return nullptr;
    }
}

void SecureClient::cleanup() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }

    if (socket_fd_ != -1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

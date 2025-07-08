#ifndef SECURE_CLIENT_H
#define SECURE_CLIENT_H

#include <string>
#include <memory>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>


class SecureClient {
public:
    SecureClient(const std::string& server_ip = "192.168.1.1",
                 int port = 5001, const std::string& cert_file = "/etc/refrigeration/cert.pem");

    void connect();
    nlohmann::json send_and_receive(const nlohmann::json& data_to_send);

private:
    std::string server_ip_;
    int port_;
    std::string cert_file_;
    int reconnect_delay_;

    int socket_fd_;
    SSL_CTX* ctx_;
    SSL* ssl_;

    void cleanup();
};

#endif // SECURE_CLIENT_H

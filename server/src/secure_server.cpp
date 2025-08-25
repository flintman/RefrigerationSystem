#include "secure_server.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <regex>
#include <random>
#include <cstdlib>
#include <numeric>
#include <unistd.h>
#include <sys/socket.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

SecureServer::SecureServer(const std::string& host, int port, int web_port, int max_attempts)
    : host_(host), port_(port), web_port_(web_port), max_attempts_(max_attempts),
      socket_server_fd_(-1), web_server_fd_(-1), running_(false) {

    load_environment_variables();
    create_data_directory();
    load_blocked_ips();
    load_data();

    // Initialize OpenSSL
    OPENSSL_init_ssl(0, NULL);
    OPENSSL_init_crypto(0, NULL);
}

SecureServer::~SecureServer() {
    stop();
    EVP_cleanup();
}

static std::string get_server_root_directory() {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + "/refrigeration-server";
        }
        return "/tmp/refrgieration-server";
    }

// Helper function to load .env file
void load_dotenv(const std::string& filename = std::string(get_server_root_directory()) + "/.env") {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Remove possible quotes
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        setenv(key.c_str(), value.c_str(), 1);
    }
}

void SecureServer::load_environment_variables() {
    // Load .env file first
    load_dotenv();

    // Load environment variables or use defaults
    const char* env_value;

    env_value = std::getenv("EMAIL_SERVER");
    email_server_ = env_value ? env_value : "";

    env_value = std::getenv("EMAIL_ADDRESS");
    email_address_ = env_value ? env_value : "";

    env_value = std::getenv("EMAIL_PASSWORD");
    email_password_ = env_value ? env_value : "";

    env_value = std::getenv("CERT_FILE");
    cert_file_ = env_value ? env_value : "";

    env_value = std::getenv("KEY_FILE");
    key_file_ = env_value ? env_value : "";

    env_value = std::getenv("CA_CERT_FILE");
    ca_cert_file_ = env_value ? env_value : "";
}

void SecureServer::create_data_directory() {
    std::string data_dir = std::string(get_server_root_directory()) + "/" + DATA_DIRECTORY;
    if (!std::filesystem::exists(data_dir)) {
        std::filesystem::create_directories(data_dir);
    }
}

void SecureServer::load_blocked_ips() {
    std::lock_guard<std::mutex> lock(blocked_ips_mutex_);
    blocked_ips_.clear();

    std::string blocked_ips_path = std::string(get_server_root_directory()) + "/" + BLOCKED_IPS_FILE;
    std::ifstream file(blocked_ips_path);
    if (file.is_open()) {
        nlohmann::json j;
        file >> j;
        if (j.is_array()) {
            for (const auto& ip : j) {
                if (ip.is_string()) {
                    blocked_ips_.insert(ip.get<std::string>());
                }
            }
        }
    }
}

void SecureServer::save_blocked_ips() {
    std::lock_guard<std::mutex> lock(blocked_ips_mutex_);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& ip : blocked_ips_) {
        j.push_back(ip);
    }

    std::string blocked_ips_path = std::string(get_server_root_directory()) + "/" + BLOCKED_IPS_FILE;
    std::ofstream file(blocked_ips_path);
    file << j.dump(4);
}

void SecureServer::load_data() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    unit_data_.clear();

    std::string data_dir = std::string(get_server_root_directory()) + "/" + DATA_DIRECTORY;
    log("Loading data from directory: " + data_dir);
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string filename = entry.path().filename().string();
            //log("Found file: " + filename);
            size_t underscore_pos = filename.find('_');
            if (underscore_pos != std::string::npos) {
                std::string unit = filename.substr(0, underscore_pos);
                //log("Processing file for unit: " + unit + ", path: " + entry.path().string());
                process_file(unit, entry.path().string());
            } else {
                log("Filename does not match expected format: " + filename);
            }
        }
    }
    log("Finished loading data. Units loaded: " + std::to_string(unit_data_.size()));
}

void SecureServer::process_file(const std::string& unit, const std::string& file_path) {
    //log("Processing file for unit: " + unit + ", path: " + file_path);
    std::ifstream file(file_path);
    if (!file.is_open()) return;

    // Check if file is empty
    if (file.peek() == std::ifstream::traits_type::eof()) {
        log("File " + file_path + " is empty. Skipping.");
        return;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        log("Error parsing JSON in file: " + file_path + " - " + e.what());
        return;
    }

    if (!j.is_array()) return;

    if (unit_data_.find(unit) == unit_data_.end()) {
        unit_data_[unit] = std::vector<nlohmann::json>();
    }

    for (const auto& record : j) {
        if (record.is_object()) {
            nlohmann::json processed_record = record;
            unit_data_[unit].push_back(processed_record);
        }
    }

    std::sort(unit_data_[unit].begin(), unit_data_[unit].end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("timestamp", "") < b.value("timestamp", "");
        });
}

void SecureServer::start() {
    running_ = true;

    // Start socket server thread
    socket_thread_ = std::thread(&SecureServer::start_socket_server, this);

    // Start web server thread
    web_thread_ = std::thread(&SecureServer::start_web_server, this);

    log("SecureServer started on socket port " + std::to_string(port_) +
        " and web port " + std::to_string(web_port_));

    // Wait for threads to complete
    if (socket_thread_.joinable()) {
        socket_thread_.join();
    }
    if (web_thread_.joinable()) {
        web_thread_.join();
    }
}

void SecureServer::stop() {
    running_ = false;

    if (socket_server_fd_ != -1) {
        shutdown(socket_server_fd_, SHUT_RDWR);
        close(socket_server_fd_);
        socket_server_fd_ = -1;
    }

    if (web_server_fd_ != -1) {
        shutdown(web_server_fd_, SHUT_RDWR);
        close(web_server_fd_);
        web_server_fd_ = -1;
    }

    if (socket_thread_.joinable()) {
        socket_thread_.join();
    }
    if (web_thread_.joinable()) {
        web_thread_.join();
    }
}

SSL_CTX* SecureServer::create_ssl_context() {
    if (cert_file_.empty() || key_file_.empty()) {
        log("SSL certificates not configured - running in test mode without SSL");
        return nullptr;
    }

    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);

    if (!ctx) {
        log("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    // Set minimum TLS version
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // Set cipher list
    SSL_CTX_set_cipher_list(ctx, "ECDHE+AESGCM:DHE+AESGCM:!aNULL:!eNULL:!MD5:!RC4");

    // Load certificate and key
    if (SSL_CTX_use_certificate_file(ctx, cert_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        log("Failed to load certificate file");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        log("Failed to load private key file");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    // Load CA certificate for client verification
    if (!ca_cert_file_.empty()) {
        if (SSL_CTX_load_verify_locations(ctx, ca_cert_file_.c_str(), nullptr) <= 0) {
            log("Failed to load CA certificate file");
            ERR_print_errors_fp(stderr);
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    }

    return ctx;
}

void SecureServer::start_socket_server() {
    SSL_CTX* ctx = create_ssl_context();
    bool use_ssl = (ctx != nullptr);

    if (!use_ssl) {
        log("WARNING: Running socket server in non-SSL mode for testing");
    }

    socket_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_server_fd_ < 0) {
        log("Failed to create socket");
        if (ctx) SSL_CTX_free(ctx);
        return;
    }

    int opt = 1;
    setsockopt(socket_server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log("Failed to bind socket server");
        close(socket_server_fd_);
        if (ctx) SSL_CTX_free(ctx);
        return;
    }

    if (listen(socket_server_fd_, 10) < 0) {
        log("Failed to listen on socket server");
        close(socket_server_fd_);
        if (ctx) SSL_CTX_free(ctx);
        return;
    }

    log("Socket server listening on " + host_ + ":" + std::to_string(port_) +
        (use_ssl ? " (SSL)" : " (non-SSL)"));

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(socket_server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                log("Failed to accept client connection");
            }
            continue;
        }

        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        log("Accepted connection from " + client_ip);

        // Check if IP is blocked
        {
            std::lock_guard<std::mutex> lock(blocked_ips_mutex_);
            if (blocked_ips_.find(client_ip) != blocked_ips_.end()) {
                log("Blocked connection attempt from " + client_ip);
                close(client_fd);
                continue;
            }
        }

        if (use_ssl) {
            // Create SSL connection
            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, client_fd);

            if (SSL_accept(ssl) <= 0) {
                log("SSL handshake failed for " + client_ip);
                ERR_print_errors_fp(stderr);

                // Track failed attempts
                failed_attempts_[client_ip]++;
                if (failed_attempts_[client_ip] >= max_attempts_) {
                    std::lock_guard<std::mutex> lock(blocked_ips_mutex_);
                    blocked_ips_.insert(client_ip);
                    save_blocked_ips();
                    log("IP " + client_ip + " blocked after " + std::to_string(max_attempts_) + " failed attempts");
                }

                SSL_free(ssl);
                close(client_fd);
                continue;
            }

            log("SSL handshake complete for " + client_ip);

            // Handle client in a separate thread
            std::thread([this, ssl, client_fd, client_ip]() {
                handle_client(ssl, client_ip);
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client_fd);
            }).detach();
        } else {
            // Handle client without SSL for testing
            std::thread([this, client_fd, client_ip]() {
                handle_client_no_ssl(client_fd, client_ip);
                close(client_fd);
            }).detach();
        }
    }

    if (ctx) SSL_CTX_free(ctx);
}

void SecureServer::handle_client(SSL* ssl, const std::string& client_ip) {
    log("Handling client " + client_ip);

    char buffer[1024] = {0};
    std::string data;

    // Read data from client via SSL
    while (running_) {
        int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            log("No more data from " + client_ip);
            break;
        }

        buffer[bytes_read] = '\0';
        data += buffer;

        // Check if we have a complete JSON message
        if (data.find('}') != std::string::npos) {
            log("End of JSON detected from " + client_ip);
            break;
        }
    }

    if (data.empty()) {
        log("No data received from " + client_ip);
        return;
    }

    try {
        nlohmann::json received_data = nlohmann::json::parse(data);
        log("Received from " + client_ip + ": " + received_data.dump());

        // Process alarm codes if they're in string format
        if (received_data.contains("alarm_codes") && received_data["alarm_codes"].is_string()) {
            std::string alarm_codes_str = received_data["alarm_codes"];
            std::vector<int> alarm_codes;
            std::stringstream ss(alarm_codes_str);
            std::string code;
            while (std::getline(ss, code, ',')) {
                code.erase(0, code.find_first_not_of(" \t"));
                code.erase(code.find_last_not_of(" \t") + 1);
                if (!code.empty() && std::all_of(code.begin(), code.end(), ::isdigit)) {
                    alarm_codes.push_back(std::stoi(code));
                }
            }
            received_data["alarm_codes"] = alarm_codes;
        }

        if (received_data.contains("unit")) {
            std::string unit = received_data["unit"];
            log("Processing data for unit " + unit + ": " + received_data.dump());
            append_data(received_data);

            nlohmann::json response;
            response["status"] = "Received";

            // Check for pending commands
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                auto it = pending_commands_.find(unit);
                if (it != pending_commands_.end()) {
                    response["status"] = it->second;
                    pending_commands_.erase(it);
                    log("Sending command " + it->second + " to Unit " + unit);
                }
            }

            // Check for alarms and send email
            if (received_data.contains("alarm_codes") && !received_data["alarm_codes"].empty()) {
                std::vector<int> current_alarms = received_data["alarm_codes"];

                // Check if alarms have changed
                bool send_email = false;
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    auto it = active_alarms_.find(unit);
                    if (it == active_alarms_.end() || it->second != current_alarms) {
                        active_alarms_[unit] = current_alarms;
                        send_email = true;
                    }
                }

                if (send_email) {
                    log("Sending email for Unit " + unit + " with alarms");
                    this->send_email(received_data);
                } else {
                    log("Alarm for Unit " + unit + " already sent. Skipping email.");
                }
            } else {
                // Clear alarms for this unit
                std::lock_guard<std::mutex> lock(data_mutex_);
                auto it = active_alarms_.find(unit);
                if (it != active_alarms_.end()) {
                    log("Unit " + unit + " alarms cleared. Ready for next alert.");
                    active_alarms_.erase(it);
                }
            }

            // Send response
            std::string response_str = response.dump();
            SSL_write(ssl, response_str.c_str(), response_str.length());
            log("Sent to " + client_ip + ": " + response_str);

            cleanup_old_data();
        }

    } catch (const std::exception& e) {
        log("Error parsing JSON from " + client_ip + ": " + e.what());
    }

    log("Client " + client_ip + " disconnected");
}

void SecureServer::handle_client_no_ssl(int client_fd, const std::string& client_ip) {
    log("Handling client " + client_ip + " (non-SSL)");

    char buffer[1024] = {0};
    std::string data;

    // Read data from client
    while (running_) {
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            log("No more data from " + client_ip);
            break;
        }

        buffer[bytes_read] = '\0';
        data += buffer;

        // Check if we have a complete JSON message
        if (data.find('}') != std::string::npos) {
            log("End of JSON detected from " + client_ip);
            break;
        }
    }

    if (data.empty()) {
        log("No data received from " + client_ip);
        return;
    }

    try {
        nlohmann::json received_data = nlohmann::json::parse(data);
        log("Received from " + client_ip + ": " + received_data.dump());

        // Process alarm codes if they're in string format
        if (received_data.contains("alarm_codes") && received_data["alarm_codes"].is_string()) {
            std::string alarm_codes_str = received_data["alarm_codes"];
            std::vector<int> alarm_codes;
            std::stringstream ss(alarm_codes_str);
            std::string code;
            while (std::getline(ss, code, ',')) {
                code.erase(0, code.find_first_not_of(" \t"));
                code.erase(code.find_last_not_of(" \t") + 1);
                if (!code.empty() && std::all_of(code.begin(), code.end(), ::isdigit)) {
                    alarm_codes.push_back(std::stoi(code));
                }
            }
            received_data["alarm_codes"] = alarm_codes;
        }

        if (received_data.contains("unit")) {
            std::string unit = received_data["unit"];
            append_data(received_data);

            nlohmann::json response;
            response["status"] = "Received";

            // Check for pending commands
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                auto it = pending_commands_.find(unit);
                if (it != pending_commands_.end()) {
                    response["status"] = it->second;
                    pending_commands_.erase(it);
                    log("Sending command " + it->second + " to Unit " + unit);
                }
            }

            // Check for alarms and send email
            if (received_data.contains("alarm_codes") && !received_data["alarm_codes"].empty()) {
                std::vector<int> current_alarms = received_data["alarm_codes"];

                // Check if alarms have changed
                bool send_email = false;
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    auto it = active_alarms_.find(unit);
                    if (it == active_alarms_.end() || it->second != current_alarms) {
                        active_alarms_[unit] = current_alarms;
                        send_email = true;
                    }
                }

                if (send_email) {
                    log("Sending email for Unit " + unit + " with alarms");
                    this->send_email(received_data);
                } else {
                    log("Alarm for Unit " + unit + " already sent. Skipping email.");
                }
            } else {
                // Clear alarms for this unit
                std::lock_guard<std::mutex> lock(data_mutex_);
                auto it = active_alarms_.find(unit);
                if (it != active_alarms_.end()) {
                    log("Unit " + unit + " alarms cleared. Ready for next alert.");
                    active_alarms_.erase(it);
                }
            }

            // Send response
            std::string response_str = response.dump();
            send(client_fd, response_str.c_str(), response_str.length(), 0);
            log("Sent to " + client_ip + ": " + response_str);

            cleanup_old_data();
        }

    } catch (const std::exception& e) {
        log("Error parsing JSON from " + client_ip + ": " + e.what());
    }

    log("Client " + client_ip + " disconnected");
}

void SecureServer::append_data(const nlohmann::json& data) {
    std::string unit = data.value("unit", "unknown");
    log("Appending data for Unit " + unit + ": " + data.dump());
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time_t);

    std::ostringstream date_stream;
    date_stream << std::put_time(tm, "%Y-%m-%d");
    std::string date_str = date_stream.str();

    std::string data_dir = std::string(get_server_root_directory()) + "/" + DATA_DIRECTORY;
    std::string filepath = data_dir + "/" + unit + "_" + date_str + ".json";

    log("Appending data for Unit " + unit + " to " + filepath);

    nlohmann::json existing_data = nlohmann::json::array();

    // Read existing file if it exists
    std::ifstream infile(filepath);
    if (infile.is_open()) {
        infile >> existing_data;
        if (!existing_data.is_array()) {
            existing_data = nlohmann::json::array({existing_data});
        }
        infile.close();
    }

    // Add new data
    existing_data.push_back(data);

    // Write back to file and flush
    {
        std::ofstream outfile(filepath);
        outfile << existing_data.dump(4);
        outfile.flush();
        outfile.close();
    }

    log("Data appended to " + filepath);

    // Reload data
    load_data();
}

void SecureServer::cleanup_old_data(int days) {
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * days);

    try {
        std::string data_dir = std::string(get_server_root_directory()) + "/" + DATA_DIRECTORY;
        for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().filename().string();

                // Extract date from filename (format: unit_YYYY-MM-DD.json)
                size_t underscore_pos = filename.find('_');
                size_t dot_pos = filename.find('.');

                if (underscore_pos != std::string::npos && dot_pos != std::string::npos) {
                    std::string date_str = filename.substr(underscore_pos + 1, dot_pos - underscore_pos - 1);

                    // Parse date (simplified - would need proper date parsing)
                    std::tm tm = {};
                    std::istringstream ss(date_str);
                    ss >> std::get_time(&tm, "%Y-%m-%d");

                    if (!ss.fail()) {
                        auto file_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

                        if (file_time < cutoff) {
                            std::filesystem::remove(entry.path());
                            log("Deleted old file: " + filename);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        log("Error during cleanup: " + std::string(e.what()));
    }
}

void SecureServer::log(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time_t);

    std::ostringstream timestamp;
    timestamp << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

    std::cout << "[" << timestamp.str() << "][TID:" << std::this_thread::get_id() << "] " << message << std::endl;
}

std::string SecureServer::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time_t);

    std::ostringstream timestamp;
    timestamp << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return timestamp.str();
}

void SecureServer::start_web_server() {
    web_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (web_server_fd_ < 0) {
        log("Failed to create web server socket");
        return;
    }

    int opt = 1;
    setsockopt(web_server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(web_port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(web_server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log("Failed to bind web server");
        close(web_server_fd_);
        return;
    }

    if (listen(web_server_fd_, 10) < 0) {
        log("Failed to listen on web server");
        close(web_server_fd_);
        return;
    }

    log("Web server listening on " + host_ + ":" + std::to_string(web_port_));

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(web_server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                log("Failed to accept web client connection");
            }
            continue;
        }

        // Handle web client in separate thread
        std::thread([this, client_fd]() {
            handle_web_client(client_fd);
            close(client_fd);
        }).detach();
    }
}

void SecureServer::handle_web_client(int client_fd) {
    char buffer[4096] = {0};
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        return;
    }

    std::string request(buffer, bytes_read);
    std::string response = process_http_request(request);

    send(client_fd, response.c_str(), response.length(), 0);
}

std::string SecureServer::process_http_request(const std::string& request) {
    std::istringstream request_stream(request);
    std::string method, path, version;
    request_stream >> method >> path >> version;

    // Parse headers
    std::map<std::string, std::string> headers = parse_http_headers(request);

    if (method == "GET") {
        return handle_get_request(path, headers);
    } else if (method == "POST") {
        // Extract body from request
        size_t body_start = request.find("\r\n\r\n");
        std::string body;
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
        return handle_post_request(path, body, headers);
    }

    return create_http_response(405, "text/plain", "Method Not Allowed");
}

std::string SecureServer::handle_get_request(const std::string& path, const std::map<std::string, std::string>& headers) {
    if (path == "/") {
        return render_template("index.html");
    } else if (path.substr(0, 8) == "/static/") {
        return serve_static_file(path.substr(8));
    } else if (path.substr(0, 6) == "/unit/") {
        std::string unit = path.substr(6);
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = unit_data_.find(unit);
        if (it != unit_data_.end()) {
            nlohmann::json response = it->second;
            return create_http_response(200, "application/json", response.dump());
        } else {
            return create_http_response(404, "application/json", "[]");
        }
    } else if (path.substr(0, 10) == "/download/") {
        std::string unit = path.substr(10);
        std::string csv_data = generate_csv_data(unit);
        if (!csv_data.empty()) {
            std::map<std::string, std::string> extra_headers;
            extra_headers["Content-Disposition"] = "attachment; filename=unit_" + unit + "_data.csv";
            return create_http_response(200, "text/csv", csv_data, extra_headers);
        } else {
            return create_http_response(404, "text/plain", "No data available");
        }
    } else if (path == "/api/units") {
        // API endpoint to get units and their data
        std::lock_guard<std::mutex> lock(data_mutex_);
        nlohmann::json response;
        response["units"] = get_unit_list();
        response["unit_data"] = unit_data_;
        return create_http_response(200, "application/json", response.dump());
    }

    return create_http_response(404, "text/plain", "Not Found");
}

std::string SecureServer::handle_post_request(const std::string& path, const std::string& body, const std::map<std::string, std::string>& headers) {
    if (path.substr(0, 9) == "/command/") {
        std::string unit = path.substr(9);

        try {
            nlohmann::json request_json = nlohmann::json::parse(body);
            if (request_json.contains("command")) {
                std::string command = request_json["command"];

                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    pending_commands_[unit] = command;
                }

                nlohmann::json response;
                response["status"] = "success";
                response["message"] = "Command " + command + " queued for " + unit;

                return create_http_response(200, "application/json", response.dump());
            } else {
                nlohmann::json error_response;
                error_response["status"] = "error";
                error_response["message"] = "Invalid command";
                return create_http_response(400, "application/json", error_response.dump());
            }
        } catch (const std::exception& e) {
            nlohmann::json error_response;
            error_response["status"] = "error";
            error_response["message"] = "Invalid JSON";
            return create_http_response(400, "application/json", error_response.dump());
        }
    }

    return create_http_response(404, "text/plain", "Not Found");
}

std::string SecureServer::create_http_response(int status_code, const std::string& content_type, const std::string& body, const std::map<std::string, std::string>& extra_headers) {
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Unknown"; break;
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";

    for (const auto& header : extra_headers) {
        response << header.first << ": " << header.second << "\r\n";
    }

    response << "\r\n";
    response << body;

    return response.str();
}

std::string SecureServer::serve_static_file(const std::string& file_path) {
    std::string full_path = std::string(get_server_root_directory()) + "/static/" + file_path;

    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return create_http_response(404, "text/plain", "File not found");
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string extension = get_file_extension(file_path);
    std::string mime_type = get_mime_type(extension);

    return create_http_response(200, mime_type, content);
}

std::string SecureServer::render_template(const std::string& template_name, const std::map<std::string, std::string>& variables) {
    std::string template_path = std::string(get_server_root_directory()) + "/templates/" + template_name;

    std::ifstream file(template_path);
    if (!file.is_open()) {
        return create_http_response(404, "text/plain", "Template not found");
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Simple variable substitution (not full templating)
    for (const auto& var : variables) {
        std::string placeholder = "{{" + var.first + "}}";
        size_t pos = content.find(placeholder);
        while (pos != std::string::npos) {
            content.replace(pos, placeholder.length(), var.second);
            pos = content.find(placeholder, pos + var.second.length());
        }
    }

    return create_http_response(200, "text/html", content);
}

std::vector<std::string> SecureServer::get_unit_list() {
    std::vector<std::string> units;
    for (const auto& pair : unit_data_) {
        units.push_back(pair.first);
    }
    std::sort(units.begin(), units.end());
    return units;
}

std::string SecureServer::generate_csv_data(const std::string& unit) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = unit_data_.find(unit);
    if (it == unit_data_.end() || it->second.empty()) {
        return "";
    }

    std::ostringstream csv;
    csv << "Timestamp,Setpoint,Return Temp,Supply Temp,Coil Temp,Fan,Compressor,Electric Heater,Valve,Status,Alarm Codes\n";

    auto get_double = [](const nlohmann::json& j, const std::string& key) -> double {
        if (!j.contains(key)) return 0.0;
        if (j[key].is_number()) return j[key].get<double>();
        if (j[key].is_string()) {
            try { return std::stod(j[key].get<std::string>()); } catch (...) { return 0.0; }
        }
        return 0.0;
    };

    auto get_bool = [](const nlohmann::json& j, const std::string& key) -> bool {
        if (!j.contains(key)) return false;
        if (j[key].is_boolean()) return j[key].get<bool>();
        if (j[key].is_string()) {
            std::string val = j[key].get<std::string>();
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            return val == "true";
        }
        return false;
    };

    // Sort records by date and time part of timestamp ascending (oldest first)
    std::vector<nlohmann::json> sorted_records = it->second;
    std::sort(sorted_records.begin(), sorted_records.end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            auto parse_datetime = [](const nlohmann::json& j) -> std::tuple<std::string, std::string> {
                std::string ts = j.value("timestamp", "");
                size_t space_pos = ts.find(' ');
                if (space_pos != std::string::npos) {
                    std::string time_part = ts.substr(0, space_pos);
                    std::string date_part = ts.substr(space_pos + 1);
                    return std::make_tuple(date_part, time_part); // (MM:DD:YYYY, HH:MM:SS)
                }
                return std::make_tuple(ts, "");
            };
            auto [date_a, time_a] = parse_datetime(a);
            auto [date_b, time_b] = parse_datetime(b);
            if (date_a == date_b) {
                return time_a < time_b;
            }
            return date_a < date_b;
        });

    for (const auto& record : sorted_records) {
        // Reformat timestamp from "HH:MM:SS MM:DD:YYYY" to "MM:DD:YYYY HH:MM:SS"
        std::string timestamp = record.value("timestamp", "N/A");
        std::string formatted_timestamp = timestamp;
        size_t space_pos = timestamp.find(' ');
        if (space_pos != std::string::npos) {
            std::string time_part = timestamp.substr(0, space_pos);
            std::string date_part = timestamp.substr(space_pos + 1);
            formatted_timestamp = date_part + " " + time_part;
        }
        csv << "\"" << formatted_timestamp << "\",";
        csv << get_double(record, "setpoint") << ",";
        csv << get_double(record, "return_temp") << ",";
        csv << get_double(record, "supply_temp") << ",";
        csv << get_double(record, "coil_temp") << ",";
        csv << (get_bool(record, "fan") ? "ON" : "OFF") << ",";
        csv << (get_bool(record, "compressor") ? "ON" : "OFF") << ",";
        csv << (get_bool(record, "electric_heater") ? "ON" : "OFF") << ",";
        csv << (get_bool(record, "valve") ? "OPEN" : "CLOSED") << ",";
        csv << "\"" << record.value("status", "N/A") << "\",";

        // Handle alarm codes
        if (record.contains("alarm_codes") && record["alarm_codes"].is_array()) {
            std::vector<std::string> alarm_strs;
            for (const auto& alarm : record["alarm_codes"]) {
                if (alarm.is_number()) {
                    alarm_strs.push_back(std::to_string(alarm.get<int>()));
                } else if (alarm.is_string()) {
                    alarm_strs.push_back(alarm.get<std::string>());
                }
            }
            if (!alarm_strs.empty()) {
                csv << "\"" << std::regex_replace(
                    std::accumulate(alarm_strs.begin(), alarm_strs.end(), std::string(),
                        [](const std::string& a, const std::string& b) {
                            return a.empty() ? b : a + ", " + b;
                        }), std::regex("\""), "\"\"") << "\"";
            } else {
                csv << "No Alarms";
            }
        } else {
            csv << "No Alarms";
        }
        csv << "\n";
    }

    return csv.str();
}

std::map<std::string, std::string> SecureServer::parse_http_headers(const std::string& request) {
    std::map<std::string, std::string> headers;
    std::istringstream stream(request);
    std::string line;

    // Skip the request line
    std::getline(stream, line);

    while (std::getline(stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            headers[key] = value;
        }
    }

    return headers;
}

std::string SecureServer::get_file_extension(const std::string& path) {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return path.substr(dot_pos + 1);
    }
    return "";
}

std::string SecureServer::get_mime_type(const std::string& extension) {
    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "json") return "application/json";
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "ico") return "image/x-icon";
    if (extension == "csv") return "text/csv";
    return "text/plain";
}

void SecureServer::send_email(const nlohmann::json& data) {
    if (!data.is_object()) {
        log("Error: Data is not a dictionary");
        return;
    }

    auto alarm_codes = data.value("alarm_codes", nlohmann::json::array());
    if (alarm_codes.empty()) {
        return;
    }

    std::string unit = data.value("unit", "N/A");

    // Build alarm codes string
    std::vector<std::string> alarm_strs;
    for (const auto& alarm : alarm_codes) {
        alarm_strs.push_back(std::to_string(alarm.get<int>()));
    }
    std::string alarm_codes_str = std::accumulate(alarm_strs.begin(), alarm_strs.end(), std::string(),
        [](const std::string& a, const std::string& b) {
            return a.empty() ? b : a + ", " + b;
        });

    std::ostringstream email_body;
    email_body << "**ALARM ALERT**\n";
    email_body << "Timestamp: " << data.value("timestamp", "N/A") << "\n";
    email_body << "Unit Number: " << unit << "\n";
    email_body << "Alarm Codes: " << alarm_codes_str << "\n\n";
    email_body << "System Status:\n";
    email_body << "- Setpoint: " << data.value("setpoint", "N/A") << "\n";
    email_body << "- Status: " << data.value("status", "N/A") << "\n";
    email_body << "- Return Temp: " << data.value("return_temp", "N/A") << "°F\n";
    email_body << "- Supply Temp: " << data.value("supply_temp", "N/A") << "°F\n";
    email_body << "- Coil Temp: " << data.value("coil_temp", "N/A") << "°F\n";

    std::string subject = "ALARM: Unit " + unit + " Detected!";

    if (!send_smtp_email(email_address_, subject, email_body.str())) {
        log("Email sent to " + email_address_ + " with Unit " + unit + " and Alarm Codes: " + alarm_codes_str);
    } else {
        log("Failed to send email");
    }
}
    struct upload_status {
        size_t bytes_read;
        const char *payload;
    };
    static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
        struct upload_status *upload_ctx = (struct upload_status *)userp;
        const char *data;

        if ((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
            return 0;
        }

        data = &upload_ctx->payload[upload_ctx->bytes_read];

        if (data[0] == '\0') {
            return 0; // end of data
        }

        size_t len = strlen(data);
        memcpy(ptr, data, len);
        upload_ctx->bytes_read += len;
        return len;
    }

bool SecureServer::send_smtp_email(const std::string& to,
                                   const std::string& subject,
                                   const std::string& body) {
    if (email_server_.empty() || email_address_.empty() || email_password_.empty()) {
        log("Email configuration incomplete - skipping email send");
        return false;
    }

    CURL *curl;
    CURLcode res = CURLE_OK;

    // Build the email payload as a single std::string
    std::string payload_text =
        "Date: Fri, 22 Aug 2025 07:00:00 -0400\r\n"
        "To: " + to + "\r\n"
        "From: REFRIGERATION-ALARM@" + email_server_ + "\r\n"
        "Subject: " + subject + "\r\n"
        "\r\n" + body + "\r\n";

    struct upload_status upload_ctx = { 0, payload_text.c_str() };

    struct curl_slist *recipients = NULL;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, email_address_.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, email_password_.c_str());

        // SMTPS (implicit TLS) on port 465:
        curl_easy_setopt(curl, CURLOPT_URL, ("smtps://" + email_server_ + ":465").c_str());

        // If using STARTTLS on 587 instead:
        // curl_easy_setopt(curl, CURLOPT_URL, ("smtp://" + email_server_ + ":587").c_str());
        // curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, email_address_.c_str());
        recipients = curl_slist_append(recipients, email_address_.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        // Now use our callback function to feed the mail body
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        // Optional: verbose output to debug
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        else
            std::cout << "Email sent successfully!" << std::endl;

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }
    return (int)res;
}

std::string SecureServer::base64_encode(const std::string& input) {
    // Simple base64 encoding implementation
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        encoded.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (encoded.size() % 4) {
        encoded.push_back('=');
    }

    return encoded;
}
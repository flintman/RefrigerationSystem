#ifndef SECURE_SERVER_H
#define SECURE_SERVER_H

#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>

class SecureServer {
public:
    SecureServer(const std::string& host = "0.0.0.0", 
                 int port = 5001, 
                 int web_port = 5008,
                 int max_attempts = 3);
    ~SecureServer();

    void start();
    void stop();

private:
    // Configuration
    std::string host_;
    int port_;
    int web_port_;
    int max_attempts_;
    
    // SSL/TLS settings
    std::string cert_file_;
    std::string key_file_;
    std::string ca_cert_file_;
    
    // Email settings
    std::string email_server_;
    std::string email_address_;
    std::string email_password_;
    
    // Server state
    int socket_server_fd_;
    int web_server_fd_;
    bool running_;
    std::thread socket_thread_;
    std::thread web_thread_;
    
    // Security
    std::set<std::string> blocked_ips_;
    std::map<std::string, int> failed_attempts_;
    mutable std::mutex blocked_ips_mutex_;
    
    // Data management
    std::map<std::string, std::vector<nlohmann::json>> unit_data_;
    std::map<std::string, std::vector<int>> active_alarms_;
    std::map<std::string, std::string> pending_commands_;
    mutable std::mutex data_mutex_;
    
    // Session management
    std::map<std::string, time_t> sessions_;
    mutable std::mutex session_mutex_;
    
    // Constants
    static constexpr int SESSION_TIMEOUT = 600;
    static constexpr const char* BLOCKED_IPS_FILE = "blocked_ips.json";
    static constexpr const char* DATA_DIRECTORY = "received_data";
    
    // Initialization
    void load_environment_variables();
    void create_data_directory();
    void load_blocked_ips();
    void save_blocked_ips();
    void load_data();
    void process_file(const std::string& unit, const std::string& file_path);
    
    // Socket server methods
    void start_socket_server();
    void handle_client(SSL* ssl, const std::string& client_ip);
    void handle_client_no_ssl(int client_fd, const std::string& client_ip);
    SSL_CTX* create_ssl_context();
    
    // Web server methods
    void start_web_server();
    void handle_web_client(int client_fd);
    std::string process_http_request(const std::string& request);
    std::string handle_get_request(const std::string& path, const std::map<std::string, std::string>& headers);
    std::string handle_post_request(const std::string& path, const std::string& body, const std::map<std::string, std::string>& headers);
    
    // HTTP response helpers
    std::string create_http_response(int status_code, const std::string& content_type, const std::string& body, const std::map<std::string, std::string>& extra_headers = {});
    std::string serve_static_file(const std::string& file_path);
    std::string render_template(const std::string& template_name, const std::map<std::string, std::string>& variables = {});
    
    // Data processing
    void append_data(const nlohmann::json& data);
    void cleanup_old_data(int days = 30);
    std::vector<std::string> get_unit_list();
    
    // Email functionality
    void send_email(const nlohmann::json& data);
    bool send_smtp_email(const std::string& to, const std::string& subject, const std::string& body);
    std::string base64_encode(const std::string& input);
    
    // Session management
    std::string generate_session_id();
    bool is_session_valid(const std::string& session_id);
    void cleanup_expired_sessions();
    
    // Utility methods
    void log(const std::string& message);
    std::string get_current_timestamp();
    std::string url_decode(const std::string& str);
    std::map<std::string, std::string> parse_http_headers(const std::string& request);
    std::map<std::string, std::string> parse_query_params(const std::string& query);
    std::string get_file_extension(const std::string& path);
    std::string get_mime_type(const std::string& extension);
    
    // Excel generation (CSV for simplicity without external deps)
    std::string generate_csv_data(const std::string& unit);
};

#endif // SECURE_SERVER_H
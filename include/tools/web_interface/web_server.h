/*
 * Web Server
 * Handles HTTP requests and static file serving
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class WebServer {
public:
    WebServer(int port = 9000);
    ~WebServer();

    void start();
    void stop();
    bool is_running() const { return running_; }

    // Set callback for API requests
    void set_get_handler(std::function<std::string(const std::string&)> handler) {
        get_handler_ = handler;
    }

    void set_post_handler(std::function<std::string(const std::string&, const std::string&)> handler) {
        post_handler_ = handler;
    }

    void set_login_verifier(std::function<bool(const std::string&)> verifier) {
        login_verifier_ = verifier;
    }

private:
    int port_;
    int server_fd_;
    bool running_;
    std::thread server_thread_;
    std::mutex server_mutex_;

    // Handlers
    std::function<std::string(const std::string&)> get_handler_;
    std::function<std::string(const std::string&, const std::string&)> post_handler_;
    std::function<bool(const std::string&)> login_verifier_;

    void server_loop();
    void handle_client(int client_fd);
    std::string process_http_request(const std::string& request);
    std::string handle_get_request(const std::string& path);
    std::string handle_post_request(const std::string& path, const std::string& body);
    std::string serve_static_file(const std::string& file_path);
    std::string read_template(const std::string& template_name);

    void write_log(const std::string& message);
};

#endif // WEB_SERVER_H

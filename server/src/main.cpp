#include "secure_server.h"
#include <iostream>
#include <csignal>
#include <memory>

std::unique_ptr<SecureServer> server;

void signal_handler(int signal) {
    if (server) {
        std::cout << "\nReceived signal " << signal << ", shutting down server..." << std::endl;
        server->stop();
    }
    exit(0);
}

int main() {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        std::cout << "Starting Secure Server..." << std::endl;
        
        // Create and start server with default parameters
        server = std::make_unique<SecureServer>();
        server->start();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
/*
 * API Web Interface Main Entry Point
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 */

#include "tools/web_interface/api_web_interface.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <thread>
#include <chrono>

static APIWebInterface* g_web_interface = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n\nShutting down API Web Interface..." << std::endl;
        if (g_web_interface) {
            g_web_interface->stop();
        }
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    std::string config_file = "web_interface_config.env";

    if (argc > 1) {
        config_file = argv[1];
    } else {
        // Try standard locations if no argument provided
        std::string locations[] = {
            "web_interface_config.env",
            "/etc/web-api/web_interface_config.env"
        };

        for (const auto& loc : locations) {
            std::ifstream test(loc);
            if (test.good()) {
                config_file = loc;
                test.close();
                break;
            }
        }
    }

    std::cout << "=== Refrigeration API Web Interface ===" << std::endl;
    std::cout << "Config File: " << config_file << std::endl;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        g_web_interface = new APIWebInterface(config_file);
        g_web_interface->start();

        // Keep the program running
        std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

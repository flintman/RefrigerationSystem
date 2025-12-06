#include "tools/system_executor.h"
#include <cstdlib>
#include <thread>
#include <chrono>

std::string SystemCommandExecutor::StartService() {
    return ExecuteCommand("sudo systemctl start " + std::string(SERVICE_NAME) + " 2>&1");
}

std::string SystemCommandExecutor::StopService() {
    return ExecuteCommand("sudo systemctl stop " + std::string(SERVICE_NAME) + " 2>&1");
}

std::string SystemCommandExecutor::RestartService() {
    return ExecuteCommand("sudo systemctl restart " + std::string(SERVICE_NAME) + " 2>&1");
}

std::string SystemCommandExecutor::ExecuteCommand(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "[Failed to run command]";

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

int SystemCommandExecutor::KillRefrigerationProcess() {
    // Step 1: Try graceful systemctl stop
    system("sudo systemctl stop refrigeration.service 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Step 2: Send SIGTERM to any remaining processes
    system("pkill -TERM refrigeration 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Step 3: Force kill any stragglers with SIGKILL
    system("pkill -KILL refrigeration 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 4: Verify all processes are gone
    int retries = 10;
    while (retries-- > 0) {
        int result = system("pgrep -x refrigeration >/dev/null 2>&1");
        if (result != 0) {
            // Process not found (pgrep returns non-zero when no matches)
            return 1;  // Successfully killed
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;  // Could not verify all processes killed
}

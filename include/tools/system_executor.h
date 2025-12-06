#pragma once

#include <string>
#include <vector>

/**
 * SystemCommandExecutor handles execution of system commands
 * Provides methods for systemctl operations and command output capture
 */
class SystemCommandExecutor {
public:
    /**
     * Start the refrigeration service
     */
    std::string StartService();

    /**
     * Stop the refrigeration service
     */
    std::string StopService();

    /**
     * Restart the refrigeration service
     */
    std::string RestartService();

    /**
     * Kill the refrigeration process
     * Returns 1 if successful, 0 if failed
     */
    int KillRefrigerationProcess();

    /**
     * Execute a command and return output
     */
    std::string ExecuteCommand(const std::string& cmd);

private:
    static constexpr const char* SERVICE_NAME = "refrigeration.service";
};

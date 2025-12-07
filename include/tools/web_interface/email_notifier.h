/*
 * Email Notifier
 * Handles alarm notifications via email
 */

#ifndef EMAIL_NOTIFIER_H
#define EMAIL_NOTIFIER_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class EmailNotifier {
public:
    EmailNotifier(const std::string& email_server, int email_port,
                  const std::string& email_address, const std::string& email_password);
    ~EmailNotifier();

    // Email sending
    bool send_alarm_email(const std::string& unit_id, const json& status_data);
    bool send_email(const std::string& to, const std::string& subject, const std::string& body);

    // Configuration
    void set_sender(const std::string& email, const std::string& password);
    void set_server(const std::string& server, int port);

private:
    std::string email_server_;
    int email_port_;
    std::string email_address_;
    std::string email_password_;

    bool send_smtp_email(const std::string& to, const std::string& subject, const std::string& body);
    std::string format_alarm_body(const std::string& unit_id, const json& status_data);

    void write_log(const std::string& message);
};

#endif // EMAIL_NOTIFIER_H

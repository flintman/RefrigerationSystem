/*
 * Email Notifier Implementation
 */

#include "../include/tools/web_interface/email_notifier.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <curl/curl.h>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <ctime>

// SMTP upload context for streaming payload
struct SMTPUploadContext {
    std::string payload;
    size_t bytes_read;
};

static size_t smtp_read_callback(void* ptr, size_t size, size_t nmemb, void* userp) {
    SMTPUploadContext* ctx = static_cast<SMTPUploadContext*>(userp);
    if (!ctx || ctx->payload.empty()) return 0;

    size_t max_to_read = size * nmemb;
    size_t remaining = ctx->payload.length() - ctx->bytes_read;

    if (remaining == 0) return 0;  // EOF reached

    size_t to_read = std::min(remaining, max_to_read);
    memcpy(ptr, ctx->payload.c_str() + ctx->bytes_read, to_read);
    ctx->bytes_read += to_read;

    return to_read;
}

EmailNotifier::EmailNotifier(const std::string& email_server, int email_port,
                             const std::string& email_address, const std::string& email_password)
    : email_server_(email_server), email_port_(email_port),
      email_address_(email_address), email_password_(email_password) {
}

EmailNotifier::~EmailNotifier() {
}

bool EmailNotifier::send_alarm_email(const std::string& unit_id, const json& status_data) {
    std::string subject = "ALARM: Unit " + unit_id + " Alarm Detected!";
    std::string body = format_alarm_body(unit_id, status_data);
    return send_smtp_email(email_address_, subject, body);
}

bool EmailNotifier::send_email(const std::string& to, const std::string& subject, const std::string& body) {
    return send_smtp_email(to, subject, body);
}

void EmailNotifier::set_sender(const std::string& email, const std::string& password) {
    email_address_ = email;
    email_password_ = password;
}

void EmailNotifier::set_server(const std::string& server, int port) {
    email_server_ = server;
    email_port_ = port;
}

bool EmailNotifier::send_smtp_email(const std::string& to, const std::string& subject, const std::string& body) {
    if (email_server_.empty() || email_address_.empty() || email_password_.empty()) {
        write_log("EMAIL ERROR: Email configuration incomplete");
        return false;
    }

    write_log("EMAIL: Attempting to send email to " + to + " via " + email_server_ + ":" + std::to_string(email_port_));

    CURL* curl = curl_easy_init();
    if (!curl) {
        write_log("EMAIL ERROR: Failed to initialize CURL");
        return false;
    }

    // Build the email payload
    std::string payload_text =
        "Date: Fri, 22 Dec 2025 12:00:00 -0500\r\n"
        "To: " + to + "\r\n"
        "From: REFRIGERATION-ALARM@" + email_server_ + "\r\n"
        "Subject: " + subject + "\r\n"
        "\r\n" + body + "\r\n";

    // Create upload context
    SMTPUploadContext upload_ctx;
    upload_ctx.payload = payload_text;
    upload_ctx.bytes_read = 0;

    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());

    curl_easy_setopt(curl, CURLOPT_USERNAME, email_address_.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, email_password_.c_str());

    // Set the URL based on port
    std::string smtp_url;
    if (email_port_ == 465) {
        smtp_url = "smtps://" + email_server_ + ":465";
        write_log("EMAIL: Using SMTPS (implicit TLS) on port 465");
    } else if (email_port_ == 587) {
        smtp_url = "smtp://" + email_server_ + ":587";
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
        write_log("EMAIL: Using SMTP with STARTTLS on port 587");
    } else {
        smtp_url = "smtp://" + email_server_ + ":" + std::to_string(email_port_);
        write_log("EMAIL: Using SMTP on port " + std::to_string(email_port_));
    }

    curl_easy_setopt(curl, CURLOPT_URL, smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, email_address_.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    // Use upload mode with payload callback
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, smtp_read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, (void*)&upload_ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // Suppress response content (we don't need it for SMTP)
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);

    write_log("EMAIL: Sending message...");
    CURLcode res = curl_easy_perform(curl);

    bool success = (res == CURLE_OK);
    if (!success) {
        write_log("EMAIL ERROR: " + std::string(curl_easy_strerror(res)));
    } else {
        write_log("EMAIL: Message sent successfully");
    }

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return success;
}

std::string EmailNotifier::format_alarm_body(const std::string& unit_id, const json& status_data) {
    std::ostringstream body;
    body << "ALARM ALERT\n\n";
    body << "Unit ID: " << unit_id << "\n";

    // Handle timestamp - convert to human-readable format
    if (status_data.contains("timestamp")) {
        std::string formatted_time;
        if (status_data["timestamp"].is_number()) {
            // Unix timestamp - convert to human-readable
            time_t timestamp = static_cast<time_t>(status_data["timestamp"].get<double>());
            struct tm* timeinfo = std::localtime(&timestamp);
            char buffer[64];
            strftime(buffer, sizeof(buffer), "%m-%d-%Y %H:%M:%S", timeinfo);
            formatted_time = buffer;
        } else if (status_data["timestamp"].is_string()) {
            formatted_time = status_data["timestamp"].get<std::string>();
        } else {
            formatted_time = "N/A";
        }
        body << "Timestamp: " << formatted_time << "\n";
    }

    body << "System Status: " << status_data.value("system_status", "Unknown") << "\n";

    // Alarm fields
    bool alarm_warning = status_data.value("alarm_warning", false);
    bool alarm_shutdown = status_data.value("alarm_shutdown", false);
    auto alarm_codes = status_data.value("active_alarms", json::array());

    body << "Alarm Warning: " << (alarm_warning ? "YES" : "NO") << "\n";
    body << "Alarm Shutdown: " << (alarm_shutdown ? "YES" : "NO") << "\n";
    body << "Active Alarm Codes: ";
    if (alarm_codes.is_array() && !alarm_codes.empty()) {
        for (const auto& code : alarm_codes) {
            body << code << " ";
        }
        body << "\n";
    } else {
        body << "None\n";
    }

    if (status_data.contains("sensors")) {
        auto sensors = status_data["sensors"];
        body << "\nSensor Readings:\n";
        body << "- Return Temp: " << sensors.value("return_temp", 0.0) << "°F\n";
        body << "- Supply Temp: " << sensors.value("supply_temp", 0.0) << "°F\n";
        body << "- Coil Temp: " << sensors.value("coil_temp", 0.0) << "°F\n";
    }

    body << "\nSetpoint: " << status_data.value("setpoint", 0.0) << "°F\n";

    return body.str();
}

void EmailNotifier::write_log(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time_t_now);

    std::cout << "[" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "] [EmailNotifier] " << message << std::endl;
}

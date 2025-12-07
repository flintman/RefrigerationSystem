/*
 * SSL/TLS Utilities for HTTPS Support
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * Handles SSL certificate generation and HTTPS connections
 */

#ifndef SSL_UTILS_H
#define SSL_UTILS_H

#include <string>
#include <memory>
#include <openssl/ssl.h>
#include <openssl/err.h>

class SSLContext {
public:
    /**
     * Initialize SSL context
     * @param cert_file Path to SSL certificate file
     * @param key_file Path to SSL key file
     * @param generate_self_signed If true and files don't exist, generate self-signed cert
     * @return true if initialization successful
     */
    static std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> create_context(
        const std::string& cert_file,
        const std::string& key_file,
        bool generate_self_signed = true);

    /**
     * Generate a self-signed certificate
     * @param cert_file Output path for certificate
     * @param key_file Output path for private key
     * @param days Certificate valid for N days
     * @return true if generation successful
     */
    static bool generate_self_signed_certificate(
        const std::string& cert_file,
        const std::string& key_file,
        int days = 365);

    /**
     * Check if certificate and key files exist
     * @param cert_file Certificate file path
     * @param key_file Key file path
     * @return true if both files exist
     */
    static bool certificates_exist(const std::string& cert_file, const std::string& key_file);

private:
    static void init_ssl();
    static void cleanup_ssl();
};

#endif // SSL_UTILS_H

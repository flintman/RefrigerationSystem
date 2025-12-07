/*
 * SSL/TLS Utilities Implementation
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 */

#include "ssl_utils.h"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

// Static initialization flag
static bool ssl_initialized = false;

void SSLContext::init_ssl() {
    if (!ssl_initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ssl_initialized = true;
    }
}

void SSLContext::cleanup_ssl() {
    if (ssl_initialized) {
        EVP_cleanup();
        ERR_free_strings();
        ssl_initialized = false;
    }
}

bool SSLContext::certificates_exist(const std::string& cert_file, const std::string& key_file) {
    struct stat buffer;
    return (stat(cert_file.c_str(), &buffer) == 0) && (stat(key_file.c_str(), &buffer) == 0);
}

bool SSLContext::generate_self_signed_certificate(
    const std::string& cert_file,
    const std::string& key_file,
    int days) {

    init_ssl();

    // Create a new RSA key
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        std::cerr << "Failed to create EVP_PKEY" << std::endl;
        return false;
    }

    RSA* rsa = RSA_new();
    BIGNUM* bne = BN_new();
    if (!rsa || !bne) {
        std::cerr << "Failed to create RSA or BIGNUM" << std::endl;
        EVP_PKEY_free(pkey);
        BN_free(bne);
        return false;
    }

    unsigned long e = RSA_F4;
    if (!BN_set_word(bne, e)) {
        std::cerr << "Failed to set BN word" << std::endl;
        EVP_PKEY_free(pkey);
        BN_free(bne);
        RSA_free(rsa);
        return false;
    }

    if (!RSA_generate_key_ex(rsa, 2048, bne, nullptr)) {
        std::cerr << "Failed to generate RSA key" << std::endl;
        EVP_PKEY_free(pkey);
        BN_free(bne);
        RSA_free(rsa);
        return false;
    }

    if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
        std::cerr << "Failed to assign RSA to EVP_PKEY" << std::endl;
        EVP_PKEY_free(pkey);
        BN_free(bne);
        RSA_free(rsa);
        return false;
    }

    BN_free(bne);

    // Create a new X509 certificate
    X509* x509 = X509_new();
    if (!x509) {
        std::cerr << "Failed to create X509 certificate" << std::endl;
        EVP_PKEY_free(pkey);
        return false;
    }

    // Set certificate version
    X509_set_version(x509, 2);

    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    // Set certificate validity period
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)60 * 60 * 24 * days);

    // Set subject name
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", -1, (unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", -1, (unsigned char*)"Refrigeration System", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", -1, (unsigned char*)"localhost", -1, -1, 0);

    // Set issuer name (self-signed)
    X509_set_issuer_name(x509, name);

    // Set public key
    X509_set_pubkey(x509, pkey);

    // Sign the certificate with the private key
    if (!X509_sign(x509, pkey, EVP_sha256())) {
        std::cerr << "Failed to sign X509 certificate" << std::endl;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    // Write private key to file
    FILE* key_file_ptr = fopen(key_file.c_str(), "wb");
    if (!key_file_ptr) {
        std::cerr << "Failed to open " << key_file << " for writing" << std::endl;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (!PEM_write_PrivateKey(key_file_ptr, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        std::cerr << "Failed to write private key to " << key_file << std::endl;
        fclose(key_file_ptr);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    fclose(key_file_ptr);

    // Write certificate to file
    FILE* cert_file_ptr = fopen(cert_file.c_str(), "wb");
    if (!cert_file_ptr) {
        std::cerr << "Failed to open " << cert_file << " for writing" << std::endl;
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (!PEM_write_X509(cert_file_ptr, x509)) {
        std::cerr << "Failed to write certificate to " << cert_file << std::endl;
        fclose(cert_file_ptr);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }
    fclose(cert_file_ptr);

    // Set file permissions to 600 for security
    chmod(cert_file.c_str(), 0600);
    chmod(key_file.c_str(), 0600);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    std::cout << "Self-signed certificate generated successfully:" << std::endl;
    std::cout << "  Certificate: " << cert_file << std::endl;
    std::cout << "  Private Key: " << key_file << std::endl;
    std::cout << "  Valid for: " << days << " days" << std::endl;

    return true;
}

std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> SSLContext::create_context(
    const std::string& cert_file,
    const std::string& key_file,
    bool generate_self_signed) {

    init_ssl();

    // Check if certificates exist
    if (!certificates_exist(cert_file, key_file)) {
        if (generate_self_signed) {
            std::cout << "Certificates not found. Generating self-signed certificate..." << std::endl;
            if (!generate_self_signed_certificate(cert_file, key_file)) {
                std::cerr << "Failed to generate self-signed certificate" << std::endl;
                return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
            }
        } else {
            std::cerr << "Certificate files not found and auto-generation disabled" << std::endl;
            return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
        }
    }

    // Create SSL context
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        std::cerr << "Failed to create SSL context" << std::endl;
        return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
    }

    // Load certificate
    if (SSL_CTX_use_certificate_file(ctx, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "Failed to load certificate: " << cert_file << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
    }

    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "Failed to load private key: " << key_file << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
    }

    // Verify that certificate and key match
    if (!SSL_CTX_check_private_key(ctx)) {
        std::cerr << "Certificate and private key do not match" << std::endl;
        SSL_CTX_free(ctx);
        return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(nullptr, &SSL_CTX_free);
    }

    // Set SSL options
    SSL_CTX_set_options(ctx, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_SINGLE_DH_USE | SSL_OP_SINGLE_ECDH_USE);

    return std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>(ctx, &SSL_CTX_free);
}

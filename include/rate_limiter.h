/*
 * Rate Limiter for API requests
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 *
 * Implements token bucket algorithm for rate limiting per IP address and API key
 */

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <ctime>

class RateLimiter {
public:
    /**
     * Initialize rate limiter with global limits
     * @param global_requests_per_minute Maximum requests per minute across all clients
     * @param per_ip_requests_per_minute Maximum requests per minute per IP address
     * @param per_key_requests_per_minute Maximum requests per minute per API key
     */
    RateLimiter(int global_requests_per_minute = 1000,
                int per_ip_requests_per_minute = 100,
                int per_key_requests_per_minute = 200);

    /**
     * Check if a request is allowed based on IP and/or API key
     * @param ip_address Client IP address
     * @param api_key Optional API key for authenticated requests
     * @return true if request is allowed, false if rate limited
     */
    bool is_allowed(const std::string& ip_address, const std::string& api_key = "");

    /**
     * Get remaining requests for an IP address
     * @param ip_address Client IP address
     * @return Number of remaining requests this minute
     */
    int get_remaining_requests(const std::string& ip_address);

    /**
     * Get time until rate limit resets for an IP address
     * @param ip_address Client IP address
     * @return Seconds until limit resets
     */
    int get_reset_time(const std::string& ip_address);

    /**
     * Reset all rate limit counters (for testing or maintenance)
     */
    void reset_all();

    /**
     * Get current statistics
     * @return JSON-compatible string with statistics
     */
    std::string get_statistics();

private:
    struct TokenBucket {
        int tokens;
        std::time_t last_refill;
    };

    std::unordered_map<std::string, TokenBucket> global_buckets_;
    std::unordered_map<std::string, TokenBucket> ip_buckets_;
    std::unordered_map<std::string, TokenBucket> key_buckets_;

    int global_requests_per_minute_;
    int per_ip_requests_per_minute_;
    int per_key_requests_per_minute_;

    mutable std::mutex mutex_;

    void refill_bucket(TokenBucket& bucket, int capacity);
    std::string extract_ip_from_request(const std::string& request_line);
};

#endif // RATE_LIMITER_H

/*
 * Rate Limiter Implementation
 * Copyright (c) 2025 William Bellvance Jr
 * Licensed under the MIT License.
 */

#include "rate_limiter.h"
#include <algorithm>
#include <sstream>

RateLimiter::RateLimiter(int global_requests_per_minute,
                         int per_ip_requests_per_minute,
                         int per_key_requests_per_minute)
    : global_requests_per_minute_(global_requests_per_minute),
      per_ip_requests_per_minute_(per_ip_requests_per_minute),
      per_key_requests_per_minute_(per_key_requests_per_minute) {
    // Initialize global bucket
    global_buckets_["global"] = {global_requests_per_minute, std::time(nullptr)};
}

void RateLimiter::refill_bucket(TokenBucket& bucket, int capacity) {
    std::time_t now = std::time(nullptr);
    std::time_t time_passed = now - bucket.last_refill;

    if (time_passed >= 60) {
        // Reset bucket if a full minute has passed
        bucket.tokens = capacity;
        bucket.last_refill = now;
    } else if (time_passed > 0) {
        // Add tokens based on time passed
        // This allows smooth refill over the minute instead of all-at-once
        int tokens_to_add = (capacity * time_passed) / 60;
        bucket.tokens = std::min(capacity, bucket.tokens + tokens_to_add);
    }
}

bool RateLimiter::is_allowed(const std::string& ip_address, const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check global limit
    auto& global_bucket = global_buckets_["global"];
    refill_bucket(global_bucket, global_requests_per_minute_);

    if (global_bucket.tokens <= 0) {
        return false;  // Global rate limit exceeded
    }

    // Check per-IP limit
    auto& ip_bucket = ip_buckets_[ip_address];
    if (ip_bucket.last_refill == 0) {
        // Initialize bucket for this IP
        ip_bucket = {per_ip_requests_per_minute_, std::time(nullptr)};
    }
    refill_bucket(ip_bucket, per_ip_requests_per_minute_);

    if (ip_bucket.tokens <= 0) {
        return false;  // Per-IP rate limit exceeded
    }

    // Check per-key limit if API key provided
    if (!api_key.empty()) {
        auto& key_bucket = key_buckets_[api_key];
        if (key_bucket.last_refill == 0) {
            // Initialize bucket for this API key
            key_bucket = {per_key_requests_per_minute_, std::time(nullptr)};
        }
        refill_bucket(key_bucket, per_key_requests_per_minute_);

        if (key_bucket.tokens <= 0) {
            return false;  // Per-key rate limit exceeded
        }

        // Consume token from key bucket
        key_bucket.tokens--;
    }

    // Consume tokens from both global and IP buckets
    global_bucket.tokens--;
    ip_bucket.tokens--;

    return true;
}

int RateLimiter::get_remaining_requests(const std::string& ip_address) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ip_buckets_.find(ip_address);
    if (it == ip_buckets_.end()) {
        return per_ip_requests_per_minute_;
    }

    TokenBucket bucket = it->second;
    refill_bucket(bucket, per_ip_requests_per_minute_);
    return std::max(0, bucket.tokens);
}

int RateLimiter::get_reset_time(const std::string& ip_address) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ip_buckets_.find(ip_address);
    if (it == ip_buckets_.end()) {
        return 0;
    }

    std::time_t now = std::time(nullptr);
    std::time_t time_since_refill = now - it->second.last_refill;
    int reset_in = 60 - time_since_refill;

    return std::max(0, reset_in);
}

void RateLimiter::reset_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    global_buckets_.clear();
    ip_buckets_.clear();
    key_buckets_.clear();
    global_buckets_["global"] = {global_requests_per_minute_, std::time(nullptr)};
}

std::string RateLimiter::get_statistics() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << "Rate Limiter Statistics:\n";
    oss << "Global limit: " << global_requests_per_minute_ << " req/min\n";
    oss << "Per-IP limit: " << per_ip_requests_per_minute_ << " req/min\n";
    oss << "Per-Key limit: " << per_key_requests_per_minute_ << " req/min\n";
    oss << "Active IPs: " << ip_buckets_.size() << "\n";
    oss << "Active API Keys: " << key_buckets_.size() << "\n";

    return oss.str();
}

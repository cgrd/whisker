// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    std::string hexDigest();

private:
    uint32_t state_[8];
    uint8_t block_[64];
    size_t block_len_ = 0;
    uint64_t total_len_ = 0;

    void compress(const uint8_t* block);
};

// Lowercase hex SHA-256 of a file's contents, or "" if it can't be read.
std::string sha256File(const std::string& path);

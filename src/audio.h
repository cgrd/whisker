// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <mutex>
#include <vector>

struct pw_stream;
struct pw_thread_loop;

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    bool init();
    void shutdown();

    void startCapture();
    std::vector<float> stopCapture();

    bool isCapturing() const;

    std::vector<float> peekRecent(size_t max_samples) const;

    struct pw_thread_loop* loop_ = nullptr;
    struct pw_stream* stream_ = nullptr;

    mutable std::mutex buffer_mutex_;
    std::vector<float> buffer_;
    std::atomic<bool> capturing_{false};
};

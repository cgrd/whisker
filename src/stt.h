// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

struct transcribe_model;
struct transcribe_session;

class SpeechToText {
public:
    SpeechToText();
    ~SpeechToText();

    bool init(const std::string& model_path, const std::string& backend = "vulkan");
    void shutdown();

    std::string transcribe(const std::vector<float>& audio);

    bool streamBegin();
    bool streamFeed(const float* data, size_t n_samples);
    std::string streamFinalize();
    void streamReset();

private:
    struct transcribe_model* model_ = nullptr;
    struct transcribe_session* session_ = nullptr;
};

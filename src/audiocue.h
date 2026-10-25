// SPDX-License-Identifier: MIT
#pragma once

class AudioCue {
public:
    bool init();
    void shutdown();
    void play(bool start);

    static void onProcess(void* userdata);

private:
    struct pw_thread_loop* loop_ = nullptr;
    struct pw_stream* stream_ = nullptr;

    float* tone_ = nullptr;
    int tone_len_ = 0;
    int tone_pos_ = 0;
    bool playing_ = false;
};

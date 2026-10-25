// SPDX-License-Identifier: MIT
#pragma once

#include <string>

class Haptic {
public:
    bool init();
    void shutdown();
    void pulse(int duration_ms, int strength);

private:
    int fd_ = -1;
    int effect_id_ = -1;

    static std::string findFFDevice();
};

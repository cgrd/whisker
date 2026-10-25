// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include <cstdint>

class Hotkey {
public:
    bool init(const std::string& combo);
    void close();
    int fd() const { return fd_; }

    // 1=pressed, -1=released, 0=no hotkey event
    int readEvent();

private:
    int fd_ = -1;
    std::vector<uint16_t> modifiers_;
    uint16_t trigger_ = 0;
    uint32_t mod_state_ = 0;
    bool active_ = false;

    static std::string findKeyboard();
};

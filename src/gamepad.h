// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <cstdint>

class Gamepad {
public:
    bool init(const std::string& button);
    void close();
    int fd() const { return fd_; }

    // 1=pressed, -1=released, 0=no gamepad event
    int readEvent();

private:
    int fd_ = -1;
    uint16_t button_ = 0;
    bool active_ = false;

    static uint16_t parseButton(const std::string& name);
    static std::string findController();
};

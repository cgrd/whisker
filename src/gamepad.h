// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <cstdint>
#include <vector>

// Codes the button may arrive as on a device from this vendor; empty if unknown.
std::vector<uint16_t> gamepadButtonCodes(const std::string& name, uint16_t vendor);

class Gamepad {
public:
    bool init(const std::string& button);
    void close();
    int fd() const { return fd_; }
    const std::string& path() const { return path_; }

    // 1=pressed, -1=released, 0=no gamepad event
    int readEvent();

private:
    int fd_ = -1;
    std::string path_;
    std::vector<uint16_t> codes_;
    bool active_ = false;

    static std::string findController();
};

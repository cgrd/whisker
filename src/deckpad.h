// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <cstddef>
#include <cstdint>

struct ButtonBit;

// 1=pressed, 0=released, -1=not an input report or unknown button
int deckButtonState(const uint8_t* report, size_t len, const std::string& button);

class Deckpad {
public:
    bool init(const std::string& button);
    void close();
    int fd() const { return fd_; }

    // 1=pressed, -1=released, 0=no change
    int readEvent();

    static bool handlesButton(const std::string& button);

private:
    int fd_ = -1;
    const ButtonBit* button_ = nullptr;
    bool active_ = false;

    static int findReportingNode();
};

// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>

enum class PedalButton { Left = 0, Center = 1, Right = 2 };

struct PedalEvent {
    PedalButton button;
    bool pressed;
};

class Pedal {
public:
    Pedal();
    ~Pedal();

    bool open();
    void close();
    bool isOpen() const;

    bool readEvent(PedalEvent& event);
    int readEventTimeout(PedalEvent& event, int timeout_ms);

    int fd() const { return hidraw_fd_; }

    static std::string discover();

private:
    int hidraw_fd_ = -1;
    int evdev_fd_ = -1;
    uint8_t prev_state_[3] = {};
    uint8_t pending_state_[3] = {};
    int pending_scan_ = -1; // next button index to check, -1 = no pending

    bool grabEvdev();
};

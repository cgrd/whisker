// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>

struct libevdev;
struct libevdev_uinput;

class VirtualKeyboard {
public:
    VirtualKeyboard();
    ~VirtualKeyboard();

    bool create();
    void destroy();

    bool emitCombo(const std::vector<uint16_t>& codes, bool pressed);
    bool typeText(const std::string& text);
    bool emitBackspace(int count);
    void releaseAll();
    static std::vector<uint16_t> parseCombo(const std::string& combo);

private:
    struct libevdev* dev_ = nullptr;
    struct libevdev_uinput* uidev_ = nullptr;
    std::set<uint16_t> held_;

    bool emitKey(uint16_t code, int value);
    bool sync();
};

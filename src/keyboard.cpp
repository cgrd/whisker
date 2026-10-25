// SPDX-License-Identifier: MIT
#include "keyboard.h"
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>

static const std::unordered_map<std::string, uint16_t>& keyMap() {
    static const std::unordered_map<std::string, uint16_t> m = {
        {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D},
        {"e", KEY_E}, {"f", KEY_F}, {"g", KEY_G}, {"h", KEY_H},
        {"i", KEY_I}, {"j", KEY_J}, {"k", KEY_K}, {"l", KEY_L},
        {"m", KEY_M}, {"n", KEY_N}, {"o", KEY_O}, {"p", KEY_P},
        {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T},
        {"u", KEY_U}, {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X},
        {"y", KEY_Y}, {"z", KEY_Z},

        {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4},
        {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8},
        {"9", KEY_9}, {"0", KEY_0},

        {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4},
        {"f5", KEY_F5}, {"f6", KEY_F6}, {"f7", KEY_F7}, {"f8", KEY_F8},
        {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
        {"f13", KEY_F13}, {"f14", KEY_F14}, {"f15", KEY_F15}, {"f16", KEY_F16},
        {"f17", KEY_F17}, {"f18", KEY_F18}, {"f19", KEY_F19}, {"f20", KEY_F20},
        {"f21", KEY_F21}, {"f22", KEY_F22}, {"f23", KEY_F23}, {"f24", KEY_F24},

        {"space", KEY_SPACE}, {"enter", KEY_ENTER}, {"tab", KEY_TAB},
        {"escape", KEY_ESC}, {"esc", KEY_ESC},
        {"backspace", KEY_BACKSPACE}, {"delete", KEY_DELETE},

        {"up", KEY_UP}, {"down", KEY_DOWN},
        {"left", KEY_LEFT}, {"right", KEY_RIGHT},
        {"home", KEY_HOME}, {"end", KEY_END},
        {"pageup", KEY_PAGEUP}, {"pagedown", KEY_PAGEDOWN},
        {"insert", KEY_INSERT},

        {"ctrl", KEY_LEFTCTRL}, {"control", KEY_LEFTCTRL}, {"leftctrl", KEY_LEFTCTRL},
        {"shift", KEY_LEFTSHIFT}, {"leftshift", KEY_LEFTSHIFT},
        {"alt", KEY_LEFTALT}, {"leftalt", KEY_LEFTALT},
        {"super", KEY_LEFTMETA}, {"meta", KEY_LEFTMETA}, {"leftmeta", KEY_LEFTMETA},

        {"playpause", KEY_PLAYPAUSE}, {"nextsong", KEY_NEXTSONG},
        {"previoussong", KEY_PREVIOUSSONG},
        {"volumeup", KEY_VOLUMEUP}, {"volumedown", KEY_VOLUMEDOWN},
        {"mute", KEY_MUTE},
    };
    return m;
}

static std::vector<uint16_t> allKeyCodes() {
    std::set<uint16_t> seen;
    for (const auto& [name, code] : keyMap())
        seen.insert(code);
    return {seen.begin(), seen.end()};
}

// Character-to-keycode mapping for typeText
struct CharMapping {
    uint16_t code;
    bool shift;
};

static const std::unordered_map<char, CharMapping>& charMap() {
    static const std::unordered_map<char, CharMapping> m = {
        {' ', {KEY_SPACE, false}}, {'\n', {KEY_ENTER, false}},
        {'\t', {KEY_TAB, false}},

        {'0', {KEY_0, false}}, {'1', {KEY_1, false}}, {'2', {KEY_2, false}},
        {'3', {KEY_3, false}}, {'4', {KEY_4, false}}, {'5', {KEY_5, false}},
        {'6', {KEY_6, false}}, {'7', {KEY_7, false}}, {'8', {KEY_8, false}},
        {'9', {KEY_9, false}},

        {'!', {KEY_1, true}}, {'@', {KEY_2, true}}, {'#', {KEY_3, true}},
        {'$', {KEY_4, true}}, {'%', {KEY_5, true}}, {'^', {KEY_6, true}},
        {'&', {KEY_7, true}}, {'*', {KEY_8, true}}, {'(', {KEY_9, true}},
        {')', {KEY_0, true}},

        {'-', {KEY_MINUS, false}}, {'_', {KEY_MINUS, true}},
        {'=', {KEY_EQUAL, false}}, {'+', {KEY_EQUAL, true}},
        {'[', {KEY_LEFTBRACE, false}}, {'{', {KEY_LEFTBRACE, true}},
        {']', {KEY_RIGHTBRACE, false}}, {'}', {KEY_RIGHTBRACE, true}},
        {'\\', {KEY_BACKSLASH, false}}, {'|', {KEY_BACKSLASH, true}},
        {';', {KEY_SEMICOLON, false}}, {':', {KEY_SEMICOLON, true}},
        {'\'', {KEY_APOSTROPHE, false}}, {'"', {KEY_APOSTROPHE, true}},
        {'`', {KEY_GRAVE, false}}, {'~', {KEY_GRAVE, true}},
        {',', {KEY_COMMA, false}}, {'<', {KEY_COMMA, true}},
        {'.', {KEY_DOT, false}}, {'>', {KEY_DOT, true}},
        {'/', {KEY_SLASH, false}}, {'?', {KEY_SLASH, true}},
    };
    return m;
}

VirtualKeyboard::VirtualKeyboard() = default;

VirtualKeyboard::~VirtualKeyboard() {
    destroy();
}

bool VirtualKeyboard::create() {
    dev_ = libevdev_new();
    if (!dev_) {
        fprintf(stderr, "keyboard: libevdev_new failed\n");
        return false;
    }

    libevdev_set_name(dev_, "whisker");

    libevdev_enable_event_type(dev_, EV_KEY);
    for (auto code : allKeyCodes())
        libevdev_enable_event_code(dev_, EV_KEY, code, nullptr);

    for (uint16_t code : {KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE,
                          KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE,
                          KEY_GRAVE, KEY_COMMA, KEY_DOT, KEY_SLASH})
        libevdev_enable_event_code(dev_, EV_KEY, code, nullptr);

    libevdev_enable_event_type(dev_, EV_REP);
    libevdev_enable_event_code(dev_, EV_REP, REP_DELAY, nullptr);
    libevdev_enable_event_code(dev_, EV_REP, REP_PERIOD, nullptr);

    int err = libevdev_uinput_create_from_device(dev_,
        LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev_);
    if (err < 0) {
        fprintf(stderr, "keyboard: uinput_create failed: %s\n", strerror(-err));
        libevdev_free(dev_);
        dev_ = nullptr;
        return false;
    }

    return true;
}

void VirtualKeyboard::destroy() {
    releaseAll();
    if (uidev_) {
        libevdev_uinput_destroy(uidev_);
        uidev_ = nullptr;
    }
    if (dev_) {
        libevdev_free(dev_);
        dev_ = nullptr;
    }
}

bool VirtualKeyboard::emitKey(uint16_t code, int value) {
    if (!uidev_)
        return false;
    int err = libevdev_uinput_write_event(uidev_, EV_KEY, code, value);
    if (err < 0) {
        fprintf(stderr, "keyboard: write_event KEY %u val %d: %s\n",
                code, value, strerror(-err));
        return false;
    }
    return true;
}

bool VirtualKeyboard::sync() {
    if (!uidev_)
        return false;
    int err = libevdev_uinput_write_event(uidev_, EV_SYN, SYN_REPORT, 0);
    if (err < 0) {
        fprintf(stderr, "keyboard: write_event SYN: %s\n", strerror(-err));
        return false;
    }
    return true;
}

bool VirtualKeyboard::emitCombo(const std::vector<uint16_t>& codes, bool pressed) {
    if (pressed) {
        for (auto code : codes) {
            if (!emitKey(code, 1))
                return false;
            held_.insert(code);
        }
    } else {
        for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
            if (!emitKey(*it, 0))
                return false;
            held_.erase(*it);
        }
    }
    return sync();
}

bool VirtualKeyboard::typeText(const std::string& text) {
    const auto& cm = charMap();

    for (char c : text) {
        if (c >= 'a' && c <= 'z') {
            uint16_t code = KEY_A + (c - 'a');
            // KEY_A..KEY_Z aren't contiguous; use the keyMap
            auto it = keyMap().find(std::string(1, c));
            if (it == keyMap().end())
                continue;
            code = it->second;
            if (!emitKey(code, 1) || !sync() || !emitKey(code, 0) || !sync())
                return false;
        }
        else if (c >= 'A' && c <= 'Z') {
            auto it = keyMap().find(std::string(1, static_cast<char>(c - 'A' + 'a')));
            if (it == keyMap().end())
                continue;
            uint16_t code = it->second;
            if (!emitKey(KEY_LEFTSHIFT, 1) || !sync() ||
                !emitKey(code, 1) || !sync() ||
                !emitKey(code, 0) || !sync() ||
                !emitKey(KEY_LEFTSHIFT, 0) || !sync())
                return false;
        }
        else {
            auto it = cm.find(c);
            if (it == cm.end())
                continue;
            const auto& [code, shift] = it->second;
            if (shift) {
                if (!emitKey(KEY_LEFTSHIFT, 1) || !sync() ||
                    !emitKey(code, 1) || !sync() ||
                    !emitKey(code, 0) || !sync() ||
                    !emitKey(KEY_LEFTSHIFT, 0) || !sync())
                    return false;
            } else {
                if (!emitKey(code, 1) || !sync() || !emitKey(code, 0) || !sync())
                    return false;
            }
        }
        usleep(2000);
    }
    return true;
}

bool VirtualKeyboard::emitBackspace(int count) {
    for (int i = 0; i < count; i++) {
        if (!emitKey(KEY_BACKSPACE, 1) || !sync() ||
            !emitKey(KEY_BACKSPACE, 0) || !sync())
            return false;
        usleep(2000);
    }
    return true;
}

void VirtualKeyboard::releaseAll() {
    if (held_.empty())
        return;
    for (auto code : held_)
        emitKey(code, 0);
    sync();
    held_.clear();
}

std::vector<uint16_t> VirtualKeyboard::parseCombo(const std::string& combo) {
    std::vector<uint16_t> codes;
    const auto& km = keyMap();

    std::string lower = combo;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    size_t start = 0;
    while (start < lower.size()) {
        // Skip leading whitespace
        while (start < lower.size() && lower[start] == ' ')
            ++start;
        if (start >= lower.size())
            break;

        size_t end = lower.find('+', start);
        if (end == std::string::npos)
            end = lower.size();

        // Trim trailing whitespace from the part
        size_t pend = end;
        while (pend > start && lower[pend - 1] == ' ')
            --pend;

        std::string part = lower.substr(start, pend - start);
        auto it = km.find(part);
        if (it != km.end())
            codes.push_back(it->second);
        else
            fprintf(stderr, "keyboard: unknown key: \"%s\"\n", part.c_str());

        start = end + 1;
    }

    return codes;
}

// SPDX-License-Identifier: MIT
#include "hotkey.h"
#include "keyboard.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>

static bool isModifier(uint16_t code) {
    return code == KEY_LEFTCTRL  || code == KEY_RIGHTCTRL ||
           code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ||
           code == KEY_LEFTALT   || code == KEY_RIGHTALT ||
           code == KEY_LEFTMETA  || code == KEY_RIGHTMETA;
}

static uint32_t modBit(uint16_t code) {
    switch (code) {
    case KEY_LEFTCTRL:  case KEY_RIGHTCTRL:  return 1u << 0;
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return 1u << 1;
    case KEY_LEFTALT:   case KEY_RIGHTALT:   return 1u << 2;
    case KEY_LEFTMETA:  case KEY_RIGHTMETA:  return 1u << 3;
    default: return 0;
    }
}

std::string Hotkey::findKeyboard() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return {};

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long evbits[EV_MAX / (sizeof(unsigned long) * 8) + 1] = {};
        unsigned long keybits[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {};
        ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);

        bool has_ev_key = evbits[EV_KEY / (sizeof(unsigned long) * 8)] &
                          (1UL << (EV_KEY % (sizeof(unsigned long) * 8)));
        if (!has_ev_key) { ::close(fd); continue; }

        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);

        auto hasKey = [&](int key) {
            return keybits[key / (sizeof(unsigned long) * 8)] &
                   (1UL << (key % (sizeof(unsigned long) * 8)));
        };

        if (hasKey(KEY_SPACE) && hasKey(KEY_A) && hasKey(KEY_LEFTCTRL)) {
            char name[256] = {};
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            if (strncmp(name, "whisker", 7) == 0) { ::close(fd); continue; }
            ::close(fd);
            closedir(dir);
            return path;
        }
        ::close(fd);
    }
    closedir(dir);
    return {};
}

bool Hotkey::init(const std::string& combo) {
    auto codes = VirtualKeyboard::parseCombo(combo);
    if (codes.empty()) {
        fprintf(stderr, "hotkey: empty combo\n");
        return false;
    }

    for (auto c : codes) {
        if (isModifier(c))
            modifiers_.push_back(c);
        else
            trigger_ = c;
    }
    if (trigger_ == 0) {
        fprintf(stderr, "hotkey: combo has no trigger key (only modifiers)\n");
        return false;
    }

    std::string path = findKeyboard();
    if (path.empty()) {
        fprintf(stderr, "hotkey: no keyboard device found\n");
        return false;
    }

    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        fprintf(stderr, "hotkey: failed to open %s\n", path.c_str());
        return false;
    }

    fprintf(stderr, "hotkey: listening on %s for %s\n", path.c_str(), combo.c_str());
    return true;
}

void Hotkey::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

int Hotkey::readEvent() {
    struct input_event ev;
    while (::read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY) continue;

        uint16_t code = ev.code;
        int value = ev.value; // 0=release, 1=press, 2=repeat

        if (isModifier(code)) {
            if (value == 1)
                mod_state_ |= modBit(code);
            else if (value == 0)
                mod_state_ &= ~modBit(code);
        }

        if (code == trigger_) {
            uint32_t required = 0;
            for (auto m : modifiers_) required |= modBit(m);

            if (value == 1 && !active_) {
                if ((mod_state_ & required) == required) {
                    active_ = true;
                    return 1;
                }
            } else if (value == 0 && active_) {
                active_ = false;
                return -1;
            }
        }
    }
    return 0;
}

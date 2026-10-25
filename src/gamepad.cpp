// SPDX-License-Identifier: MIT
#include "gamepad.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

uint16_t Gamepad::parseButton(const std::string& name) {
    if (name == "A")      return BTN_SOUTH;
    if (name == "B")      return BTN_EAST;
    if (name == "X")      return BTN_WEST;
    if (name == "Y")      return BTN_NORTH;
    if (name == "L1")     return BTN_TL;
    if (name == "R1")     return BTN_TR;
    if (name == "L2")     return BTN_TL2;
    if (name == "R2")     return BTN_TR2;
    if (name == "select") return BTN_SELECT;
    if (name == "start")  return BTN_START;
    if (name == "guide")  return BTN_MODE;
    if (name == "L3")     return BTN_THUMBL;
    if (name == "R3")     return BTN_THUMBR;
    if (name == "L4")     return BTN_TRIGGER_HAPPY13;
    if (name == "L5")     return BTN_TRIGGER_HAPPY14;
    if (name == "R4")     return BTN_TRIGGER_HAPPY15;
    if (name == "R5")     return BTN_TRIGGER_HAPPY16;

    char* end = nullptr;
    unsigned long code = strtoul(name.c_str(), &end, 10);
    if (end != name.c_str() && *end == '\0' && code <= UINT16_MAX)
        return static_cast<uint16_t>(code);

    return 0;
}

std::string Gamepad::findController() {
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
        bool has_ev_abs = evbits[EV_ABS / (sizeof(unsigned long) * 8)] &
                          (1UL << (EV_ABS % (sizeof(unsigned long) * 8)));
        if (!has_ev_key || !has_ev_abs) { ::close(fd); continue; }

        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);

        auto hasKey = [&](int key) {
            return keybits[key / (sizeof(unsigned long) * 8)] &
                   (1UL << (key % (sizeof(unsigned long) * 8)));
        };

        if (hasKey(BTN_SOUTH) && hasKey(BTN_EAST)) {
            char devname[256] = {};
            ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);
            ::close(fd);
            closedir(dir);
            fprintf(stderr, "gamepad: found %s at %s\n", devname, path.c_str());
            return path;
        }
        ::close(fd);
    }
    closedir(dir);
    return {};
}

bool Gamepad::init(const std::string& button) {
    button_ = parseButton(button);
    if (button_ == 0) {
        fprintf(stderr, "gamepad: unknown button '%s'\n", button.c_str());
        return false;
    }

    std::string path = findController();
    if (path.empty()) {
        fprintf(stderr, "gamepad: no controller found\n");
        return false;
    }

    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        fprintf(stderr, "gamepad: failed to open %s\n", path.c_str());
        return false;
    }

    fprintf(stderr, "gamepad: listening on %s for %s\n", path.c_str(), button.c_str());
    return true;
}

void Gamepad::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

int Gamepad::readEvent() {
    struct input_event ev;
    while (::read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY) continue;
        if (ev.code != button_) continue;

        if (ev.value == 1 && !active_) {
            active_ = true;
            return 1;
        } else if (ev.value == 0 && active_) {
            active_ = false;
            return -1;
        }
    }
    return 0;
}

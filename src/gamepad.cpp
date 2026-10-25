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
#include <algorithm>

// Kernel headers before 6.17 lack these.
#ifndef BTN_GRIPL
#define BTN_GRIPL  0x224
#define BTN_GRIPR  0x225
#define BTN_GRIPL2 0x226
#define BTN_GRIPR2 0x227
#endif

static constexpr uint16_t kValveVendor = 0x28DE;
static constexpr uint16_t kMicrosoftVendor = 0x045E;

struct BackButton {
    const char* name;
    uint16_t code;
    uint16_t legacy_valve;
    uint16_t legacy_xbox;
};

// Linux 6.17 moved hid-steam (Deck) and xpad (Elite paddles) to BTN_GRIP*.
// Older kernels used BTN_TRIGGER_HAPPY1-4 and 5-8, which other drivers use for other buttons.
static constexpr BackButton kBackButtons[] = {
    {"L4", BTN_GRIPL, BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY7},
    {"R4", BTN_GRIPR, BTN_TRIGGER_HAPPY2, BTN_TRIGGER_HAPPY5},
    {"L5", BTN_GRIPL2, BTN_TRIGGER_HAPPY3, BTN_TRIGGER_HAPPY8},
    {"R5", BTN_GRIPR2, BTN_TRIGGER_HAPPY4, BTN_TRIGGER_HAPPY6},
};

static uint16_t standardButton(const std::string& name) {
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

    char* end = nullptr;
    unsigned long code = strtoul(name.c_str(), &end, 10);
    if (end != name.c_str() && *end == '\0' && code <= UINT16_MAX)
        return static_cast<uint16_t>(code);

    return 0;
}

std::vector<uint16_t> gamepadButtonCodes(const std::string& name, uint16_t vendor) {
    for (const auto& b : kBackButtons) {
        if (name != b.name) continue;
        if (vendor == kValveVendor) return {b.code, b.legacy_valve};
        if (vendor == kMicrosoftVendor) return {b.code, b.legacy_xbox};
        return {b.code};
    }
    uint16_t code = standardButton(name);
    if (code == 0) return {};
    return {code};
}

static uint16_t deviceVendor(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    input_id id = {};
    ioctl(fd, EVIOCGID, &id);
    ::close(fd);
    return id.vendor;
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
    if (gamepadButtonCodes(button, 0).empty()) {
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

    path_ = path;
    codes_ = gamepadButtonCodes(button, deviceVendor(path));
    fprintf(stderr, "gamepad: listening on %s for %s\n", path.c_str(), button.c_str());
    return true;
}

void Gamepad::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    path_.clear();
}

int Gamepad::readEvent() {
    struct input_event ev;
    while (::read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_KEY) continue;
        if (std::find(codes_.begin(), codes_.end(), ev.code) == codes_.end()) continue;

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

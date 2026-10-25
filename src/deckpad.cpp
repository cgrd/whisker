// SPDX-License-Identifier: MIT
#include "deckpad.h"
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <glob.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static constexpr int kReportLen = 64;
static constexpr int kProbeTimeoutMs = 1000;

struct ButtonBit {
    const char* name;
    int byte_offset;
    int bit;
};

static constexpr ButtonBit kButtons[] = {
    {"L5", 8, 15},
    {"R5", 8, 16},
    {"L4", 12, 9},
    {"R4", 12, 10},
};

static const ButtonBit* lookupButton(const std::string& name) {
    for (auto& b : kButtons)
        if (name == b.name) return &b;
    return nullptr;
}

static bool isInputReport(const uint8_t* data, ssize_t len) {
    return len == kReportLen && data[0] == 0x01 && data[2] == 0x09;
}

static uint32_t readLE32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static int buttonState(const uint8_t* report, ssize_t len, const ButtonBit& b) {
    if (!isInputReport(report, len)) return -1;
    return (readLE32(report + b.byte_offset) >> b.bit) & 1;
}

int deckButtonState(const uint8_t* report, size_t len, const std::string& button) {
    const ButtonBit* b = lookupButton(button);
    return b ? buttonState(report, static_cast<ssize_t>(len), *b) : -1;
}

static bool isDeckController(const std::string& uevent_path) {
    std::ifstream f(uevent_path);
    std::string line;
    while (std::getline(f, line))
        if (line == "HID_ID=0003:000028DE:00001205") return true;
    return false;
}

bool Deckpad::handlesButton(const std::string& button) {
    return lookupButton(button) != nullptr;
}

// Several hidraw nodes share the Deck's VID/PID; only one streams input reports.
int openDeckControllerNode(int flags) {
    glob_t g;
    if (glob("/sys/class/hidraw/*/device/uevent", 0, nullptr, &g) != 0) return -1;

    std::vector<pollfd> fds;
    std::vector<std::string> paths;
    for (size_t i = 0; i < g.gl_pathc; ++i) {
        if (!isDeckController(g.gl_pathv[i])) continue;
        std::string node = g.gl_pathv[i];
        node = "/dev/" + node.substr(strlen("/sys/class/hidraw/"),
                                     node.find("/device/") - strlen("/sys/class/hidraw/"));
        int fd = ::open(node.c_str(), flags | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "deckpad: cannot open %s: %s\n", node.c_str(), strerror(errno));
            continue;
        }
        fds.push_back({fd, POLLIN, 0});
        paths.push_back(node);
    }
    globfree(&g);

    int found = -1;
    for (int waited = 0; found < 0 && !fds.empty() && waited < kProbeTimeoutMs; waited += 50) {
        if (::poll(fds.data(), fds.size(), 50) < 0) break;
        for (size_t i = 0; i < fds.size() && found < 0; ++i) {
            if (!(fds[i].revents & POLLIN)) continue;
            uint8_t buf[kReportLen];
            ssize_t n;
            while ((n = ::read(fds[i].fd, buf, sizeof(buf))) > 0) {
                if (isInputReport(buf, n)) {
                    found = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    for (size_t i = 0; i < fds.size(); ++i)
        if (static_cast<int>(i) != found) ::close(fds[i].fd);
    if (found < 0) return -1;

    fprintf(stderr, "deckpad: found Steam Deck controller at %s\n", paths[found].c_str());
    return fds[found].fd;
}

bool Deckpad::init(const std::string& button) {
    const ButtonBit* b = lookupButton(button);
    if (!b) return false;

    fd_ = openDeckControllerNode(O_RDONLY);
    if (fd_ < 0) return false;

    button_ = b;
    fprintf(stderr, "deckpad: listening for %s\n", button.c_str());
    return true;
}

void Deckpad::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

int Deckpad::readEvent() {
    uint8_t buf[kReportLen];
    ssize_t n;
    while ((n = ::read(fd_, buf, sizeof(buf))) > 0) {
        int state = buttonState(buf, n, *button_);
        if (state < 0) continue;
        bool pressed = state == 1;
        if (pressed == active_) continue;
        active_ = pressed;
        return pressed ? 1 : -1;
    }
    return 0;
}

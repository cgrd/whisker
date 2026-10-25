// SPDX-License-Identifier: MIT
#include "pedal.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

static constexpr int kReportLen = 8;
static constexpr const char* kVendorID = "FD9";
static constexpr const char* kProductID = "86";

static std::string readFileContents(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return {};
    char buf[1024];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return {};
    buf[n] = '\0';
    return buf;
}

static std::string trimLeadingZeros(const std::string& s) {
    size_t start = 0;
    while (start < s.size() - 1 && s[start] == '0') ++start;
    return s.substr(start);
}

static void toUpper(std::string& s) {
    for (auto& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
}

Pedal::Pedal() = default;

Pedal::~Pedal() { close(); }

std::string Pedal::discover() {
    DIR* dir = opendir("/sys/class/hidraw");
    if (!dir) return {};

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0) continue;

        std::string uevent = std::string("/sys/class/hidraw/") + entry->d_name + "/device/uevent";
        std::string content = readFileContents(uevent);
        if (content.empty()) continue;
        toUpper(content);

        size_t pos = content.find("HID_ID=");
        if (pos == std::string::npos) continue;

        size_t eol = content.find('\n', pos);
        std::string line = content.substr(pos + 7, eol - pos - 7);

        // HID_ID=0003:00000FD9:00000086
        size_t c1 = line.find(':');
        size_t c2 = (c1 != std::string::npos) ? line.find(':', c1 + 1) : std::string::npos;
        if (c2 == std::string::npos) continue;

        std::string vid = trimLeadingZeros(line.substr(c1 + 1, c2 - c1 - 1));
        std::string pid = trimLeadingZeros(line.substr(c2 + 1));
        while (!pid.empty() && (pid.back() == '\r' || pid.back() == '\n' || pid.back() == ' '))
            pid.pop_back();

        if (vid == kVendorID && pid == kProductID) {
            std::string path = std::string("/dev/") + entry->d_name;
            closedir(dir);
            return path;
        }
    }

    closedir(dir);
    return {};
}

bool Pedal::grabEvdev() {
    if (hidraw_fd_ < 0) return false;

    char link[256];
    char target[256];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", hidraw_fd_);
    ssize_t len = readlink(link, target, sizeof(target) - 1);
    if (len <= 0) return false;
    target[len] = '\0';

    const char* base = strrchr(target, '/');
    if (!base) return false;
    ++base;

    std::string inputDir = std::string("/sys/class/hidraw/") + base + "/device/input";
    DIR* dir = opendir(inputDir.c_str());
    if (!dir) return false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "input", 5) != 0) continue;

        std::string evDir = inputDir + "/" + entry->d_name;
        DIR* evd = opendir(evDir.c_str());
        if (!evd) continue;

        struct dirent* ev;
        while ((ev = readdir(evd)) != nullptr) {
            if (strncmp(ev->d_name, "event", 5) != 0) continue;

            std::string devPath = std::string("/dev/input/") + ev->d_name;
            int fd = ::open(devPath.c_str(), O_RDONLY);
            if (fd < 0) continue;

            int grab = 1;
            if (ioctl(fd, EVIOCGRAB, &grab) == 0) {
                evdev_fd_ = fd;
                closedir(evd);
                closedir(dir);
                return true;
            }
            ::close(fd);
        }
        closedir(evd);
    }

    closedir(dir);
    fprintf(stderr, "pedal: failed to grab evdev device\n");
    return false;
}

bool Pedal::open() {
    if (hidraw_fd_ >= 0) return true;

    std::string path = discover();
    if (path.empty()) {
        fprintf(stderr, "pedal: device not found\n");
        return false;
    }

    hidraw_fd_ = ::open(path.c_str(), O_RDONLY);
    if (hidraw_fd_ < 0) {
        fprintf(stderr, "pedal: failed to open %s\n", path.c_str());
        return false;
    }

    memset(prev_state_, 0, sizeof(prev_state_));
    pending_scan_ = -1;

    if (!grabEvdev()) {
        fprintf(stderr, "pedal: warning: could not grab evdev (phantom keys may leak)\n");
    }

    fprintf(stderr, "pedal: opened %s\n", path.c_str());
    return true;
}

void Pedal::close() {
    if (evdev_fd_ >= 0) {
        ::close(evdev_fd_);
        evdev_fd_ = -1;
    }
    if (hidraw_fd_ >= 0) {
        ::close(hidraw_fd_);
        hidraw_fd_ = -1;
    }
    memset(prev_state_, 0, sizeof(prev_state_));
    pending_scan_ = -1;
}

bool Pedal::isOpen() const { return hidraw_fd_ >= 0; }

int Pedal::readEventTimeout(PedalEvent& event, int timeout_ms) {
    if (pending_scan_ >= 0) {
        for (int i = pending_scan_; i < 3; ++i) {
            if (pending_state_[i] != prev_state_[i]) {
                event.button = static_cast<PedalButton>(i);
                event.pressed = pending_state_[i] != 0;
                prev_state_[i] = pending_state_[i];
                pending_scan_ = i + 1;
                if (pending_scan_ >= 3) pending_scan_ = -1;
                return 1;
            }
        }
        pending_scan_ = -1;
    }

    struct pollfd pfd = {hidraw_fd_, POLLIN, 0};
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return ret; // 0 = timeout, -1 = error

    uint8_t buf[kReportLen];
    ssize_t n = ::read(hidraw_fd_, buf, sizeof(buf));
    if (n < 7) return -1;

    uint8_t* state = buf + 4;
    for (int i = 0; i < 3; ++i) {
        if (state[i] != prev_state_[i]) {
            event.button = static_cast<PedalButton>(i);
            event.pressed = state[i] != 0;
            prev_state_[i] = state[i];
            memcpy(pending_state_, state, 3);
            pending_scan_ = i + 1;
            if (pending_scan_ >= 3) pending_scan_ = -1;
            return 1;
        }
    }
    return 0;
}

bool Pedal::readEvent(PedalEvent& event) {
    if (pending_scan_ >= 0) {
        for (int i = pending_scan_; i < 3; ++i) {
            if (pending_state_[i] != prev_state_[i]) {
                event.button = static_cast<PedalButton>(i);
                event.pressed = pending_state_[i] != 0;
                prev_state_[i] = pending_state_[i];
                pending_scan_ = i + 1;
                if (pending_scan_ >= 3) pending_scan_ = -1;
                return true;
            }
        }
        pending_scan_ = -1;
    }

    while (true) {
        uint8_t buf[kReportLen];
        ssize_t n = ::read(hidraw_fd_, buf, sizeof(buf));
        if (n < 7) return false;

        uint8_t* state = buf + 4;

        for (int i = 0; i < 3; ++i) {
            if (state[i] != prev_state_[i]) {
                event.button = static_cast<PedalButton>(i);
                event.pressed = state[i] != 0;
                prev_state_[i] = state[i];
                memcpy(pending_state_, state, 3);
                pending_scan_ = i + 1;
                if (pending_scan_ >= 3) pending_scan_ = -1;
                return true;
            }
        }
    }
}

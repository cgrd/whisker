// SPDX-License-Identifier: MIT
#include "controllers.h"
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

static constexpr uint16_t kValveVendor = 0x28DE;
static constexpr size_t kMaxReportLen = 64;
static constexpr int kMaxEvents = 16;

using ChangeHandler = std::function<bool(InputDevice&, ButtonSet)>;

static bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string resolve(const std::string& path) {
    char resolved[PATH_MAX];
    return realpath(path.c_str(), resolved) ? resolved : "";
}

static int openReadWrite(const std::string& path, int& flags_used) {
    flags_used = O_RDWR;
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0 || errno != EACCES) return fd;
    flags_used = O_RDONLY;
    return ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

static bool hasBit(const unsigned long* bits, int bit) {
    constexpr int kBitsPerLong = sizeof(unsigned long) * 8;
    return bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong));
}

// Valve hidraw nodes with a known decoder. A node that never streams state reports (the
// Deck's keyboard and mouse interfaces, empty puck slots) is harmless.
static bool describeHidraw(const std::string& node, InputDevice& dev) {
    std::string uevent = readFile("/sys/class/hidraw/" + node + "/device/uevent");
    if (!parseHidId(uevent, dev.vendor, dev.product)) return false;
    if (dev.vendor != kValveVendor || !valveFamily(dev.product)) return false;
    dev.hidraw = true;
    dev.name = valveFamily(dev.product)->name;
    dev.rumble = hidrawRumbleKind(dev.product);
    return true;
}

// Gamepads other than Valve's: those are read over hidraw, and Steam's virtual pad (28DE:11FF)
// would double every press.
static bool describeEvdev(int fd, const std::string& node, InputDevice& dev) {
    unsigned long evbits[EV_MAX / (sizeof(unsigned long) * 8) + 1] = {};
    unsigned long keybits[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {};
    unsigned long ffbits[FF_MAX / (sizeof(unsigned long) * 8) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return false;
    if (!hasBit(evbits, EV_KEY) || !hasBit(evbits, EV_ABS)) return false;
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    if (!hasBit(keybits, BTN_SOUTH) || !hasBit(keybits, BTN_EAST)) return false;

    input_id id = {};
    ioctl(fd, EVIOCGID, &id);
    if (id.vendor == kValveVendor) return false;

    char name[256] = {};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    dev.name = name;
    dev.vendor = id.vendor;
    dev.product = id.product;
    dev.sysfs_path = resolve("/sys/class/input/" + node + "/device");
    bool rumble = ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffbits)), ffbits) >= 0 && hasBit(ffbits, FF_RUMBLE);
    if (rumble && !isVirtualInput(dev)) dev.rumble = RumbleKind::Evdev;
    return true;
}

// The evdev buffer overflowed (SYN_DROPPED), so rebuild the held keys from the kernel's state.
static ButtonSet currentEvdevKeys(const InputDevice& dev) {
    unsigned long keybits[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {};
    if (ioctl(dev.fd, EVIOCGKEY(sizeof(keybits)), keybits) < 0) return dev.buttons;
    constexpr ButtonSet kDpad = kButtonUp | kButtonDown | kButtonLeft | kButtonRight;
    ButtonSet held = dev.buttons & kDpad;
    for (int code = BTN_MISC; code <= KEY_MAX; ++code)
        if (hasBit(keybits, code)) held = applyEvdevEvent(held, EV_KEY, code, 1, dev.vendor);
    return held;
}

bool Controllers::init(const std::string& trigger) {
    if (!trigger.empty() && !parseButtonChord(trigger, trigger_))
        fprintf(stderr, "controller: invalid trigger_button '%s'\n", trigger.c_str());

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        fprintf(stderr, "controller: epoll_create1 failed: %s\n", strerror(errno));
        return false;
    }
    if (trigger_) {
        fprintf(stderr, "controller: listening for %s\n", formatButtonChord(trigger_).c_str());
        openAll();
    }
    return true;
}

// An open Valve hidraw node counts as a hid-steam client, which hides the kernel's evdev
// gamepad and lizard mode for that device, so nothing is opened until it's needed.
void Controllers::openAll() {
    if (opened_) return;
    opened_ = true;
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ >= 0) {
        inotify_add_watch(inotify_fd_, "/dev", IN_CREATE | IN_ATTRIB | IN_DELETE);
        inotify_add_watch(inotify_fd_, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE);
        epoll_event ev = {};
        ev.events = EPOLLIN;
        ev.data.fd = inotify_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, inotify_fd_, &ev);
    } else {
        fprintf(stderr, "controller: no hotplug, inotify failed: %s\n", strerror(errno));
    }

    scan();
}

void Controllers::close() {
    for (auto& d : devices_) {
        hapticRelease(d);
        ::close(d.fd);
    }
    devices_.clear();
    opened_ = false;
    if (inotify_fd_ >= 0) { ::close(inotify_fd_); inotify_fd_ = -1; }
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }
}

void Controllers::scan() {
    const std::pair<const char*, const char*> dirs[] = {{"/dev", "hidraw"}, {"/dev/input", "event"}};
    for (const auto& [dir, prefix] : dirs) {
        DIR* d = opendir(dir);
        if (!d) continue;
        while (dirent* ent = readdir(d)) {
            std::string name = ent->d_name;
            if (startsWith(name, prefix)) openNode(std::string(dir) + "/" + name);
        }
        closedir(d);
    }
}

bool Controllers::isOpen(const std::string& path) const {
    for (const auto& d : devices_)
        if (d.path == path) return true;
    return false;
}

int Controllers::findByFd(int fd) const {
    for (size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].fd == fd) return static_cast<int>(i);
    return -1;
}

void Controllers::openNode(const std::string& path) {
    if (isOpen(path)) return;
    std::string node = path.substr(path.rfind('/') + 1);
    bool hidraw = startsWith(node, "hidraw");

    InputDevice dev;
    if (hidraw && !describeHidraw(node, dev)) return;

    int flags = 0;
    int fd = openReadWrite(path, flags);
    if (fd < 0) {
        if (hidraw)
            fprintf(stderr, "controller: cannot open %s (%s): %s\n", path.c_str(), dev.name.c_str(), strerror(errno));
        return;
    }
    if (!hidraw && !describeEvdev(fd, node, dev)) {
        ::close(fd);
        return;
    }
    if (flags == O_RDONLY) dev.rumble = RumbleKind::None;

    dev.path = path;
    dev.fd = fd;
    epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        fprintf(stderr, "controller: epoll_ctl %s failed: %s\n", path.c_str(), strerror(errno));
        ::close(fd);
        return;
    }
    bool untested = hidraw && !valveFamily(dev.product)->tested;
    fprintf(stderr, "controller: opened %s at %s%s%s\n", dev.name.c_str(), path.c_str(),
            dev.rumble == RumbleKind::None ? "" : " (rumble)", untested ? " (untested decoder)" : "");
    devices_.push_back(dev);
}

void Controllers::removeDevice(int fd) {
    int i = findByFd(fd);
    if (i < 0) return;
    fprintf(stderr, "controller: %s at %s disconnected\n", devices_[i].name.c_str(), devices_[i].path.c_str());
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    devices_.erase(devices_.begin() + i);
}

// udev applies permissions after the node appears, so retry on IN_ATTRIB.
void Controllers::handleHotplug() {
    alignas(inotify_event) char buf[4096];
    ssize_t n;
    while ((n = ::read(inotify_fd_, buf, sizeof(buf))) > 0) {
        for (char* p = buf; p < buf + n;) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            p += sizeof(inotify_event) + ev->len;
            if (!ev->len) continue;
            std::string name = ev->name;
            std::string path;
            if (startsWith(name, "hidraw")) path = "/dev/" + name;
            else if (startsWith(name, "event")) path = "/dev/input/" + name;
            else continue;
            if (ev->mask & IN_DELETE) {
                for (const auto& d : devices_)
                    if (d.path == path) { removeDevice(d.fd); break; }
            } else {
                openNode(path);
            }
        }
    }
}

bool Controllers::readDevice(InputDevice& dev, const ChangeHandler& on_change) {
    auto apply = [&](ButtonSet next) {
        if (next == dev.buttons) return false;
        ButtonSet before = dev.buttons;
        dev.buttons = next;
        return on_change(dev, before);
    };

    ssize_t n;
    if (dev.hidraw) {
        uint8_t buf[kMaxReportLen];
        while ((n = ::read(dev.fd, buf, sizeof(buf))) > 0) {
            ButtonSet next;
            if (!decodeValveReport(dev.product, buf, static_cast<size_t>(n), next)) continue;
            if (!dev.streaming) {
                dev.streaming = true;
                fprintf(stderr, "controller: %s streaming on %s\n", dev.name.c_str(), dev.path.c_str());
            }
            if (apply(next)) return true;
        }
    } else {
        input_event ev;
        while ((n = ::read(dev.fd, &ev, sizeof(ev))) == sizeof(ev)) {
            ButtonSet next = ev.type == EV_SYN && ev.code == SYN_DROPPED
                ? currentEvdevKeys(dev) : applyEvdevEvent(dev.buttons, ev.type, ev.code, ev.value, dev.vendor);
            if (apply(next)) return true;
        }
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return false;
    bool handled = apply(0);
    removeDevice(dev.fd);
    return handled;
}

bool Controllers::drain(const ChangeHandler& on_change) {
    epoll_event evs[kMaxEvents];
    int n = epoll_wait(epoll_fd_, evs, kMaxEvents, 0);
    for (int i = 0; i < n; ++i) {
        int fd = evs[i].data.fd;
        if (fd == inotify_fd_) {
            handleHotplug();
            continue;
        }
        int idx = findByFd(fd);
        if (idx < 0) continue;
        if (readDevice(devices_[idx], on_change)) return true;
    }
    return false;
}

int Controllers::readEvent() {
    int result = 0;
    drain([&](InputDevice& dev, ButtonSet before) {
        result = chordTransition(before, dev.buttons, trigger_);
        if (result == 0) return false;
        if (result == 1) last_trigger_ = dev.path;
        fprintf(stderr, "controller: trigger %s on %s\n", result == 1 ? "down" : "up", dev.path.c_str());
        return true;
    });
    return result;
}

std::string Controllers::capture(int timeout_ms, const volatile sig_atomic_t& running) {
    openAll();
    std::map<std::string, ChordCapture> captures;
    for (const auto& d : devices_) captures[d.path].armed = d.buttons == 0;
    std::string result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    fprintf(stderr, "controller: waiting for a button press\n");
    while (running && result.empty() && std::chrono::steady_clock::now() < deadline) {
        pollfd pfd = {epoll_fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 100) <= 0) continue;
        drain([&](InputDevice& dev, ButtonSet) {
            if (!captures[dev.path].update(dev.buttons)) return false;
            result = formatButtonChord(captures[dev.path].peak) + " " + dev.name;
            return true;
        });
    }
    fprintf(stderr, "controller: captured '%s'\n", result.c_str());
    return result;
}

void Controllers::pulse(const std::string& trigger_path, int duration_ms, int strength) {
    int trigger = -1;
    for (size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].path == trigger_path) trigger = static_cast<int>(i);
    int target = chooseHapticDevice(devices_, trigger);
    if (target >= 0) hapticPulse(devices_[target], duration_ms, strength);
}

// SPDX-License-Identifier: MIT
#include "haptic.h"
#include "deckpad.h"
#include <linux/hidraw.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static constexpr uint16_t kValveVendor = 0x28DE;
static constexpr uint16_t kSteamVirtualPad = 0x11FF;
static constexpr uint8_t kDeckRumbleCmd = 0xEB;

bool isVirtualInput(const InputDeviceInfo& dev) {
    if (dev.vendor == kValveVendor && dev.product == kSteamVirtualPad) return true;
    return dev.sysfs_path.find("/devices/virtual/") != std::string::npos;
}

HapticTarget chooseHapticTarget(const InputDeviceInfo* trigger, bool deck_present) {
    if (trigger && trigger->rumble && !isVirtualInput(*trigger)) return HapticTarget::TriggerPad;
    if (deck_present) return HapticTarget::DeckController;
    return HapticTarget::None;
}

// Same layout as hid-steam's steam_haptic_rumble(), behind report ID 0.
DeckFeatureReport deckRumbleReport(uint16_t left_speed, uint16_t right_speed) {
    DeckFeatureReport r{};
    r[1] = kDeckRumbleCmd;
    r[2] = 9;
    r[6] = left_speed & 0xFF;
    r[7] = left_speed >> 8;
    r[8] = right_speed & 0xFF;
    r[9] = right_speed >> 8;
    r[10] = 2;
    return r;
}

static bool hasBit(const unsigned long* bits, int bit) {
    constexpr int kBitsPerLong = sizeof(unsigned long) * 8;
    return bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong));
}

static bool describeEventNode(const std::string& path, InputDeviceInfo& info) {
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    input_id id = {};
    unsigned long ffbits[FF_MAX / (sizeof(unsigned long) * 8) + 1] = {};
    bool ok = ioctl(fd, EVIOCGID, &id) >= 0;
    info.vendor = id.vendor;
    info.product = id.product;
    info.rumble = ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffbits)), ffbits) >= 0 && hasBit(ffbits, FF_RUMBLE);
    ::close(fd);

    std::string sys = "/sys/class/input/" + path.substr(path.rfind('/') + 1) + "/device";
    char resolved[PATH_MAX];
    if (realpath(sys.c_str(), resolved)) info.sysfs_path = resolved;
    return ok;
}

bool Haptic::init(const std::string& trigger_event_path) {
    InputDeviceInfo trigger;
    bool has_trigger = !trigger_event_path.empty() && describeEventNode(trigger_event_path, trigger);

    const InputDeviceInfo* trigger_info = has_trigger ? &trigger : nullptr;

    int deck_fd = -1;
    target_ = chooseHapticTarget(trigger_info, false);
    if (target_ == HapticTarget::None) {
        deck_fd = openDeckControllerNode(O_RDWR);
        target_ = chooseHapticTarget(trigger_info, deck_fd >= 0);
    }
    switch (target_) {
    case HapticTarget::TriggerPad:
        return initEvdev(trigger_event_path);
    case HapticTarget::DeckController:
        fd_ = deck_fd;
        fprintf(stderr, "haptic: using the Steam Deck controller\n");
        return true;
    case HapticTarget::None:
        fprintf(stderr, "haptic: no physical rumble device for the trigger\n");
        return false;
    }
    return false;
}

bool Haptic::initEvdev(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        fprintf(stderr, "haptic: failed to open %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.replay.length = 150;
    if (ioctl(fd_, EVIOCSFF, &effect) < 0) {
        fprintf(stderr, "haptic: failed to upload effect: %s\n", strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    effect_id_ = effect.id;
    fprintf(stderr, "haptic: using %s (effect id %d)\n", path.c_str(), effect_id_);
    return true;
}

void Haptic::shutdown() {
    if (fd_ < 0) return;
    stopRumble();
    if (effect_id_ >= 0) {
        ioctl(fd_, EVIOCRMFF, effect_id_);
        effect_id_ = -1;
    }
    ::close(fd_);
    fd_ = -1;
    target_ = HapticTarget::None;
}

static void writePlay(int fd, int effect_id, int value) {
    input_event ev = {};
    ev.type = EV_FF;
    ev.code = static_cast<uint16_t>(effect_id);
    ev.value = value;
    if (::write(fd, &ev, sizeof(ev)) != sizeof(ev))
        fprintf(stderr, "haptic: write failed: %s\n", strerror(errno));
}

static void sendDeckReport(int fd, const DeckFeatureReport& report) {
    if (ioctl(fd, HIDIOCSFEATURE(report.size()), report.data()) < 0)
        fprintf(stderr, "haptic: deck rumble report failed: %s\n", strerror(errno));
}

void Haptic::setRumble(int duration_ms, uint16_t strength) {
    if (target_ == HapticTarget::DeckController) {
        sendDeckReport(fd_, deckRumbleReport(strength, strength / 2));
        return;
    }
    ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = static_cast<int16_t>(effect_id_);
    effect.replay.length = static_cast<uint16_t>(duration_ms);
    effect.u.rumble.strong_magnitude = strength;
    effect.u.rumble.weak_magnitude = strength / 2;
    if (ioctl(fd_, EVIOCSFF, &effect) < 0)
        fprintf(stderr, "haptic: failed to update effect: %s\n", strerror(errno));
    writePlay(fd_, effect_id_, 1);
}

void Haptic::stopRumble() {
    if (target_ == HapticTarget::DeckController)
        sendDeckReport(fd_, deckRumbleReport(0, 0));
    else if (effect_id_ >= 0)
        writePlay(fd_, effect_id_, 0);
}

void Haptic::pulse(int duration_ms, int strength) {
    if (fd_ < 0 || target_ == HapticTarget::None) return;
    setRumble(duration_ms, static_cast<uint16_t>(strength));
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    stopRumble();
}

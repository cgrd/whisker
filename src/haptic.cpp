// SPDX-License-Identifier: MIT
#include "haptic.h"
#include <linux/hidraw.h>
#include <linux/input.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>

static constexpr uint16_t kValveVendor = 0x28DE;
static constexpr uint16_t kSteamVirtualPad = 0x11FF;
static constexpr uint16_t kSteamDeck = 0x1205;
static constexpr uint8_t kDeckRumbleCmd = 0xEB;
static constexpr uint8_t kTritonRumbleReport = 0x80;
// The controller stops a rumble report on its own unless it is resent (SDL resends every 40 ms).
static constexpr int kTritonResendMs = 40;

bool isVirtualInput(const InputDevice& dev) {
    if (dev.vendor == kValveVendor && dev.product == kSteamVirtualPad) return true;
    return dev.sysfs_path.find("/devices/virtual/") != std::string::npos;
}

RumbleKind hidrawRumbleKind(uint16_t product) {
    switch (product) {
    case kSteamDeck: return RumbleKind::DeckFeature;
    case 0x1302:
    case 0x1303:
    case 0x1304:
    case 0x1305: return RumbleKind::TritonOutput;
    }
    return RumbleKind::None;
}

int chooseHapticDevice(const std::vector<InputDevice>& devices, int trigger) {
    if (trigger >= 0 && devices[trigger].rumble != RumbleKind::None) return trigger;
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        if (d.hidraw && d.product == kSteamDeck && d.streaming) return static_cast<int>(i);
    }
    return -1;
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

// MsgHapticRumble from SDL's controller_structs.h: type, intensity, then speed and gain per side.
TritonRumbleReport tritonRumbleReport(uint16_t left_speed, uint16_t right_speed) {
    TritonRumbleReport r{};
    r[0] = kTritonRumbleReport;
    r[4] = left_speed & 0xFF;
    r[5] = left_speed >> 8;
    r[7] = right_speed & 0xFF;
    r[8] = right_speed >> 8;
    return r;
}

static void writePlay(const InputDevice& dev, int value) {
    input_event ev = {};
    ev.type = EV_FF;
    ev.code = static_cast<uint16_t>(dev.effect_id);
    ev.value = value;
    if (::write(dev.fd, &ev, sizeof(ev)) != sizeof(ev))
        fprintf(stderr, "haptic: write to %s failed: %s\n", dev.path.c_str(), strerror(errno));
}

static bool uploadEffect(InputDevice& dev, int duration_ms, uint16_t strength) {
    ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = static_cast<int16_t>(dev.effect_id);
    effect.replay.length = static_cast<uint16_t>(duration_ms);
    effect.u.rumble.strong_magnitude = strength;
    effect.u.rumble.weak_magnitude = strength / 2;
    if (ioctl(dev.fd, EVIOCSFF, &effect) < 0) {
        fprintf(stderr, "haptic: failed to upload effect to %s: %s\n", dev.path.c_str(), strerror(errno));
        return false;
    }
    dev.effect_id = effect.id;
    return true;
}

static void sendDeckReport(const InputDevice& dev, const DeckFeatureReport& report) {
    if (ioctl(dev.fd, HIDIOCSFEATURE(report.size()), report.data()) < 0)
        fprintf(stderr, "haptic: deck rumble on %s failed: %s\n", dev.path.c_str(), strerror(errno));
}

static void sendTritonReport(const InputDevice& dev, const TritonRumbleReport& report) {
    if (::write(dev.fd, report.data(), report.size()) != static_cast<ssize_t>(report.size()))
        fprintf(stderr, "haptic: rumble on %s failed: %s\n", dev.path.c_str(), strerror(errno));
}

static void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void hapticPulse(InputDevice& dev, int duration_ms, int strength) {
    auto s = static_cast<uint16_t>(strength);
    if (dev.rumble != RumbleKind::None) fprintf(stderr, "haptic: %d ms pulse on %s\n", duration_ms, dev.path.c_str());
    switch (dev.rumble) {
    case RumbleKind::None:
        return;
    case RumbleKind::Evdev:
        if (!uploadEffect(dev, duration_ms, s)) return;
        writePlay(dev, 1);
        sleepMs(duration_ms);
        writePlay(dev, 0);
        return;
    case RumbleKind::DeckFeature:
        sendDeckReport(dev, deckRumbleReport(s, s / 2));
        sleepMs(duration_ms);
        sendDeckReport(dev, deckRumbleReport(0, 0));
        return;
    case RumbleKind::TritonOutput:
        for (int left = duration_ms; left > 0; left -= kTritonResendMs) {
            sendTritonReport(dev, tritonRumbleReport(s, s / 2));
            sleepMs(std::min(left, kTritonResendMs));
        }
        sendTritonReport(dev, tritonRumbleReport(0, 0));
        return;
    }
}

void hapticRelease(InputDevice& dev) {
    if (dev.rumble != RumbleKind::Evdev || dev.effect_id < 0) return;
    writePlay(dev, 0);
    ioctl(dev.fd, EVIOCRMFF, dev.effect_id);
    dev.effect_id = -1;
}

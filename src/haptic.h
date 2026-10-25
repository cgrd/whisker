// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <string>

struct InputDeviceInfo {
    std::string sysfs_path;
    uint16_t vendor = 0;
    uint16_t product = 0;
    bool rumble = false;
};

enum class HapticTarget { TriggerPad, DeckController, None };

bool isVirtualInput(const InputDeviceInfo& dev);
HapticTarget chooseHapticTarget(const InputDeviceInfo* trigger, bool deck_present);

using DeckFeatureReport = std::array<uint8_t, 65>;
DeckFeatureReport deckRumbleReport(uint16_t left_speed, uint16_t right_speed);

class Haptic {
public:
    bool init(const std::string& trigger_event_path);
    void shutdown();
    // Blocks for duration_ms so every pulse ends with an explicit stop.
    void pulse(int duration_ms, int strength);

private:
    HapticTarget target_ = HapticTarget::None;
    int fd_ = -1;
    int effect_id_ = -1;

    bool initEvdev(const std::string& path);
    void setRumble(int duration_ms, uint16_t strength);
    void stopRumble();
};

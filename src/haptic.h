// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class RumbleKind { None, Evdev, DeckFeature, TritonOutput };

struct InputDevice {
    std::string path;
    std::string name;
    std::string sysfs_path;
    int fd = -1;
    bool hidraw = false;
    bool streaming = false;
    uint16_t vendor = 0;
    uint16_t product = 0;
    uint32_t buttons = 0;
    RumbleKind rumble = RumbleKind::None;
    int effect_id = -1;
};

bool isVirtualInput(const InputDevice& dev);
RumbleKind hidrawRumbleKind(uint16_t product);

// The trigger device if it can rumble, otherwise the Steam Deck's built-in controller,
// otherwise -1. trigger is an index into devices or -1.
int chooseHapticDevice(const std::vector<InputDevice>& devices, int trigger);

using DeckFeatureReport = std::array<uint8_t, 65>;
DeckFeatureReport deckRumbleReport(uint16_t left_speed, uint16_t right_speed);

using TritonRumbleReport = std::array<uint8_t, 10>;
TritonRumbleReport tritonRumbleReport(uint16_t left_speed, uint16_t right_speed);

// Blocks for duration_ms so every pulse ends with an explicit stop.
void hapticPulse(InputDevice& dev, int duration_ms, int strength);
void hapticRelease(InputDevice& dev);

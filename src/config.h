// SPDX-License-Identifier: MIT
#pragma once
#include "chat_profile.h"
#include <array>
#include <string>

enum class PedalAction { None, Key, Transcribe, Exec };
enum class PedalMode { Tap, Hold };
enum class OutputMode { Type, Clipboard, Both };

struct PedalConfig {
    PedalAction action = PedalAction::None;
    std::string key;
    PedalMode mode = PedalMode::Tap;
    OutputMode output = OutputMode::Type;
    std::string suffix;
    std::string exec_press;
    std::string exec_release;
};

struct Config {
    std::string model_path;
    std::string backend = "vulkan";
    std::string audio_device;
    std::string trigger_key;
    std::string trigger_button;
    std::string trigger_mode = "hold";
    std::string overlay_position = "bottom";
    std::string submit_key = "none";
    int history_count = 10;
    float noise_threshold = 0.005f;
    bool overlay = true;
    bool tray = true;
    bool audio_cue = true;
    bool haptic = true;
    bool dictation_commands = true;
    bool auto_capitalize = true;
    bool undo_enabled = true;
    int recording_timeout = 30;
    std::array<PedalConfig, 3> pedals; // [0]=left, [1]=center, [2]=right
    std::string chat_profile = kChatProfileAuto;
    ChatProfiles chat_profiles;
};

Config loadConfig(const std::string& path);
void saveConfig(const Config& cfg, const std::string& path);
std::string defaultConfigPath();
std::string historyDir();
std::string modelsDir();

// Returns the key to press after typing text, or "" for none.
std::string effectiveSubmitKey(const Config& cfg, const PedalConfig& pedal, const std::string& text);

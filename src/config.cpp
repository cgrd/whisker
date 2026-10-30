// SPDX-License-Identifier: MIT
#include "config.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

static std::string expandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~')
        return path;
    const char* home = std::getenv("HOME");
    if (!home)
        return path;
    return std::string(home) + path.substr(1);
}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static int indentLevel(const std::string& line) {
    int spaces = 0;
    for (char c : line) {
        if (c == ' ')
            spaces++;
        else if (c == '\t')
            spaces += 2;
        else
            break;
    }
    return spaces / 2;
}

static PedalAction parseAction(const std::string& val) {
    if (val == "key") return PedalAction::Key;
    if (val == "transcribe") return PedalAction::Transcribe;
    if (val == "exec") return PedalAction::Exec;
    return PedalAction::None;
}

static PedalMode parseMode(const std::string& val) {
    if (val == "tap") return PedalMode::Tap;
    if (val == "hold") return PedalMode::Hold;
    return PedalMode::Hold;
}

static OutputMode parseOutput(const std::string& val) {
    if (val == "clipboard") return OutputMode::Clipboard;
    if (val == "both") return OutputMode::Both;
    return OutputMode::Type;
}

static int pedalIndex(const std::string& name) {
    if (name == "left") return 0;
    if (name == "center") return 1;
    if (name == "right") return 2;
    return -1;
}

static bool parseBool(const std::string& val) {
    return val == "true" || val == "yes" || val == "on";
}

static std::vector<std::string> splitAliases(std::string list) {
    if (list.size() >= 2 && list.front() == '[' && list.back() == ']')
        list = list.substr(1, list.size() - 2);
    std::vector<std::string> aliases;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        std::string alias = stripQuotes(trim(list.substr(start, comma - start)));
        if (!alias.empty())
            aliases.push_back(alias);
        start = comma + 1;
    }
    return aliases;
}

static std::string joinAliases(const std::vector<std::string>& aliases) {
    std::string out;
    for (const auto& a : aliases)
        out += (out.empty() ? "" : ", ") + a;
    return out;
}

enum class Section { None, Pedals, ChatProfiles };
enum class ChatSection { None, Channels, Names };

static Config defaultConfig() {
    Config cfg;
    cfg.pedals[1].action = PedalAction::Transcribe;
    cfg.trigger_key = "ctrl+shift+space";
    cfg.trigger_button = "L5";
    return cfg;
}

std::string defaultConfigPath() {
    return expandTilde("~/.config/pedal-stt/config.yaml");
}

std::string historyDir() {
    return expandTilde("~/.local/share/pedal-stt/history");
}

std::string modelsDir() {
    std::string dir = expandTilde("~/.local/share/pedal-stt/models");
    mkdir(dir.c_str(), 0755);
    return dir;
}

static void parseChatProfileLine(Config& cfg, int level, const std::string& key, const std::string& val,
                                 ChatProfile*& profile, ChatSection& section, ChatChannel*& channel) {
    if (level == 1) {
        profile = &cfg.chat_profiles[key];
        section = ChatSection::None;
        channel = nullptr;
    } else if (!profile) {
        return;
    } else if (level == 2) {
        section = ChatSection::None;
        channel = nullptr;
        if (key == "open_key")
            profile->open_key = val;
        else if (key == "channels")
            section = ChatSection::Channels;
        else if (key == "names")
            section = ChatSection::Names;
        else
            std::cerr << "whisker: config: unknown chat profile key '" << key << "'\n";
    } else if (level == 3 && section == ChatSection::Names) {
        profile->names.emplace_back(key, val);
    } else if (level == 3 && section == ChatSection::Channels) {
        profile->channels.push_back({key, "", {}, false});
        channel = &profile->channels.back();
    } else if (level == 4 && channel) {
        if (key == "command")
            channel->command = val;
        else if (key == "say")
            channel->say = splitAliases(val);
        else if (key == "target")
            channel->target = parseBool(val);
        else
            std::cerr << "whisker: config: unknown chat channel key '" << key << "'\n";
    }
}

Config loadConfig(const std::string& path) {
    std::string expanded = expandTilde(path);
    std::ifstream file(expanded);
    if (!file.is_open())
        return defaultConfig();

    Config cfg;
    Section section = Section::None;
    int current_pedal = -1;
    ChatProfile* profile = nullptr;
    ChatSection chat_section = ChatSection::None;
    ChatChannel* channel = nullptr;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || trim(line).empty() || trim(line)[0] == '#')
            continue;

        int level = indentLevel(line);
        std::string trimmed = trim(line);

        auto colon = trimmed.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key = trim(trimmed.substr(0, colon));
        std::string val = trim(trimmed.substr(colon + 1));
        val = stripQuotes(val);

        if (level == 0) {
            section = Section::None;
            current_pedal = -1;
            profile = nullptr;

            if (key == "model") {
                cfg.model_path = expandTilde(val);
            } else if (key == "backend") {
                cfg.backend = val;
            } else if (key == "history") {
                cfg.history_count = std::stoi(val);
            } else if (key == "overlay") {
                cfg.overlay = (val == "true" || val == "yes" || val == "on");
            } else if (key == "tray") {
                cfg.tray = (val == "true" || val == "yes" || val == "on");
            } else if (key == "audio_device") {
                cfg.audio_device = val;
            } else if (key == "noise_threshold") {
                cfg.noise_threshold = std::stof(val);
            } else if (key == "trigger_key") {
                cfg.trigger_key = val;
            } else if (key == "trigger_button") {
                cfg.trigger_button = val;
            } else if (key == "trigger_mode") {
                cfg.trigger_mode = val;
            } else if (key == "overlay_position") {
                cfg.overlay_position = val;
            } else if (key == "submit_key") {
                cfg.submit_key = val;
            } else if (key == "audio_cue") {
                cfg.audio_cue = (val == "true" || val == "yes" || val == "on");
            } else if (key == "haptic") {
                cfg.haptic = (val == "true" || val == "yes" || val == "on");
            } else if (key == "dictation_commands") {
                cfg.dictation_commands = (val == "true" || val == "yes" || val == "on");
            } else if (key == "auto_capitalize") {
                cfg.auto_capitalize = (val == "true" || val == "yes" || val == "on");
            } else if (key == "undo_enabled") {
                cfg.undo_enabled = (val == "true" || val == "yes" || val == "on");
            } else if (key == "recording_timeout") {
                cfg.recording_timeout = std::stoi(val);
            } else if (key == "pedals") {
                section = Section::Pedals;
            } else if (key == "chat_profile") {
                cfg.chat_profile = val;
            } else if (key == "chat_profiles") {
                section = Section::ChatProfiles;
            } else {
                std::cerr << "whisker: config: unknown key '" << key << "'\n";
            }
        } else if (section == Section::ChatProfiles) {
            parseChatProfileLine(cfg, level, key, val, profile, chat_section, channel);
        } else if (level == 1 && section == Section::Pedals) {
            int idx = pedalIndex(key);
            if (idx >= 0) {
                current_pedal = idx;
            } else {
                std::cerr << "whisker: config: unknown pedal '" << key << "'\n";
                current_pedal = -1;
            }
        } else if (level == 2 && section == Section::Pedals && current_pedal >= 0) {
            auto& pedal = cfg.pedals[current_pedal];
            if (key == "action") {
                pedal.action = parseAction(val);
                if (pedal.action == PedalAction::Key)
                    pedal.mode = PedalMode::Hold;
            } else if (key == "key") {
                pedal.key = val;
            } else if (key == "mode") {
                pedal.mode = parseMode(val);
            } else if (key == "exec_press") {
                pedal.exec_press = val;
            } else if (key == "exec_release") {
                pedal.exec_release = val;
            } else if (key == "output") {
                pedal.output = parseOutput(val);
            } else if (key == "suffix") {
                pedal.suffix = val;
            } else {
                std::cerr << "whisker: config: unknown pedal key '" << key << "'\n";
            }
        }
    }

    return cfg;
}

static std::string actionStr(PedalAction a) {
    switch (a) {
    case PedalAction::Key: return "key";
    case PedalAction::Transcribe: return "transcribe";
    case PedalAction::Exec: return "exec";
    default: return "none";
    }
}

static std::string modeStr(PedalMode m) {
    return m == PedalMode::Tap ? "tap" : "hold";
}

static std::string outputStr(OutputMode m) {
    switch (m) {
    case OutputMode::Clipboard: return "clipboard";
    case OutputMode::Both: return "both";
    default: return "type";
    }
}

static std::string collapseTilde(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (!home) return path;
    std::string h(home);
    if (path.substr(0, h.size()) == h)
        return "~" + path.substr(h.size());
    return path;
}

void saveConfig(const Config& cfg, const std::string& path) {
    std::string expanded = expandTilde(path);

    std::string dir = expanded.substr(0, expanded.rfind('/'));
    mkdir(dir.c_str(), 0755);

    std::ofstream f(expanded);

    if (!cfg.model_path.empty())
        f << "model: " << collapseTilde(cfg.model_path) << "\n";
    f << "backend: " << cfg.backend << "\n";
    if (!cfg.audio_device.empty())
        f << "audio_device: " << cfg.audio_device << "\n";
    f << "history: " << cfg.history_count << "\n";
    f << "noise_threshold: " << cfg.noise_threshold << "\n";
    f << "overlay: " << (cfg.overlay ? "true" : "false") << "\n";
    f << "tray: " << (cfg.tray ? "true" : "false") << "\n";
    if (!cfg.trigger_key.empty())
        f << "trigger_key: " << cfg.trigger_key << "\n";
    if (!cfg.trigger_button.empty())
        f << "trigger_button: " << cfg.trigger_button << "\n";
    f << "trigger_mode: " << cfg.trigger_mode << "\n";
    f << "overlay_position: " << cfg.overlay_position << "\n";
    f << "submit_key: " << cfg.submit_key << "\n";
    f << "audio_cue: " << (cfg.audio_cue ? "true" : "false") << "\n";
    f << "haptic: " << (cfg.haptic ? "true" : "false") << "\n";
    f << "dictation_commands: " << (cfg.dictation_commands ? "true" : "false") << "\n";
    f << "auto_capitalize: " << (cfg.auto_capitalize ? "true" : "false") << "\n";
    f << "undo_enabled: " << (cfg.undo_enabled ? "true" : "false") << "\n";
    f << "recording_timeout: " << cfg.recording_timeout << "\n";
    f << "\npedals:\n";

    const char* names[] = {"left", "center", "right"};
    for (int i = 0; i < 3; ++i) {
        auto& p = cfg.pedals[i];
        f << "  " << names[i] << ":\n";
        f << "    action: " << actionStr(p.action) << "\n";
        if (p.action == PedalAction::Key) {
            f << "    key: \"" << p.key << "\"\n";
            f << "    mode: " << modeStr(p.mode) << "\n";
        }
        if (p.action == PedalAction::Transcribe) {
            f << "    output: " << outputStr(p.output) << "\n";
            if (!p.suffix.empty() && p.suffix != "none")
                f << "    suffix: " << p.suffix << "\n";
        }
        if (p.action == PedalAction::Exec) {
            if (!p.exec_press.empty())
                f << "    exec_press: \"" << p.exec_press << "\"\n";
            if (!p.exec_release.empty())
                f << "    exec_release: \"" << p.exec_release << "\"\n";
        }
    }

    if (!cfg.chat_profile.empty())
        f << "\nchat_profile: " << cfg.chat_profile << "\n";
    if (!cfg.chat_profiles.empty())
        f << "\nchat_profiles:\n";
    for (const auto& [name, profile] : cfg.chat_profiles) {
        f << "  " << name << ":\n";
        if (!profile.open_key.empty())
            f << "    open_key: " << profile.open_key << "\n";
        if (!profile.channels.empty())
            f << "    channels:\n";
        for (const auto& ch : profile.channels) {
            f << "      " << ch.name << ":\n";
            f << "        command: \"" << ch.command << "\"\n";
            f << "        say: \"" << joinAliases(ch.say) << "\"\n";
            if (ch.target)
                f << "        target: true\n";
        }
        if (!profile.names.empty())
            f << "    names:\n";
        for (const auto& [spoken, name] : profile.names)
            f << "      " << spoken << ": \"" << name << "\"\n";
    }
}

std::string effectiveSubmitKey(const Config& cfg, const PedalConfig& pedal, const std::string& text) {
    const std::string& key = cfg.submit_key != "none" ? cfg.submit_key : pedal.suffix;
    if (text.empty() || key.empty() || key == "none")
        return "";
    if (key == "enter" && text.back() == '\n')
        return "";
    return key;
}

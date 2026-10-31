// SPDX-License-Identifier: MIT
#include "check.h"
#include "config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static std::string g_dir;

static std::string writeFile(const std::string& name, const std::string& text) {
    std::string path = g_dir + "/" + name;
    std::ofstream(path) << text;
    return path;
}

static void testMissingFileGivesDefaults() {
    Config cfg = loadConfig(g_dir + "/does-not-exist.yaml");
    CHECK_EQ(cfg.trigger_key, "ctrl+shift+space");
    CHECK_EQ(cfg.trigger_button, "L5");
    CHECK(cfg.pedals[1].action == PedalAction::Transcribe);
    CHECK_EQ(cfg.submit_key, "none");
    // The user picks the model; a fresh config must not point at one
    CHECK_EQ(cfg.model_path, "");
}

static void testExistingModelIsKept() {
    Config cfg = loadConfig(writeFile("existing.yaml", "model: /opt/models/parakeet-unified-en-0.6b-Q8_0.gguf\n"));
    CHECK_EQ(cfg.model_path, "/opt/models/parakeet-unified-en-0.6b-Q8_0.gguf");
    std::string saved = g_dir + "/saved.yaml";
    saveConfig(Config{}, saved);
    CHECK_EQ(loadConfig(saved).model_path, "");
}

static void testParseTopLevel() {
    Config cfg = loadConfig(writeFile("top.yaml",
        "# comment\n"
        "model: ~/models/m.gguf\n"
        "backend: cpu\n"
        "history: 50\n"
        "noise_threshold: 0.02\n"
        "overlay: false\n"
        "tray: no\n"
        "audio_device: \"alsa_input.usb\"\n"
        "trigger_key: ctrl+alt+r\n"
        "trigger_button: R4\n"
        "trigger_mode: toggle\n"
        "overlay_position: top-left\n"
        "submit_key: enter\n"
        "audio_cue: off\n"
        "haptic: false\n"
        "dictation_commands: false\n"
        "auto_capitalize: false\n"
        "undo_enabled: false\n"
        "recording_timeout: 60\n"
        "auto_update: false\n"));
    CHECK_EQ(cfg.model_path, std::string(getenv("HOME")) + "/models/m.gguf");
    CHECK_EQ(cfg.backend, "cpu");
    CHECK_EQ(cfg.history_count, 50);
    CHECK(cfg.noise_threshold > 0.0199f && cfg.noise_threshold < 0.0201f);
    CHECK(!cfg.overlay);
    CHECK(!cfg.tray);
    CHECK_EQ(cfg.audio_device, "alsa_input.usb");
    CHECK_EQ(cfg.trigger_key, "ctrl+alt+r");
    CHECK_EQ(cfg.trigger_button, "R4");
    CHECK_EQ(cfg.trigger_mode, "toggle");
    CHECK_EQ(cfg.overlay_position, "top-left");
    CHECK_EQ(cfg.submit_key, "enter");
    CHECK(!cfg.audio_cue);
    CHECK(!cfg.haptic);
    CHECK(!cfg.dictation_commands);
    CHECK(!cfg.auto_capitalize);
    CHECK(!cfg.undo_enabled);
    CHECK_EQ(cfg.recording_timeout, 60);
    CHECK(!cfg.auto_update);
}

static void testParsePedals() {
    Config cfg = loadConfig(writeFile("pedals.yaml",
        "pedals:\n"
        "  left:\n"
        "    action: key\n"
        "    key: \"ctrl+shift+alt+F1\"\n"
        "  center:\n"
        "    action: transcribe\n"
        "    # comment inside block\n"
        "    output: both\n"
        "    suffix: tab\n"
        "  right:\n"
        "    action: exec\n"
        "    exec_press: \"notify-send down\"\n"
        "    exec_release: \"notify-send up\"\n"
        "  bogus:\n"
        "    action: key\n"
        "backend: cpu\n"));
    CHECK(cfg.pedals[0].action == PedalAction::Key);
    CHECK_EQ(cfg.pedals[0].key, "ctrl+shift+alt+F1");
    CHECK(cfg.pedals[0].mode == PedalMode::Hold);
    CHECK(cfg.pedals[1].action == PedalAction::Transcribe);
    CHECK(cfg.pedals[1].output == OutputMode::Both);
    CHECK_EQ(cfg.pedals[1].suffix, "tab");
    CHECK(cfg.pedals[2].action == PedalAction::Exec);
    CHECK_EQ(cfg.pedals[2].exec_press, "notify-send down");
    CHECK_EQ(cfg.pedals[2].exec_release, "notify-send up");
    CHECK_EQ(cfg.backend, "cpu");
    CHECK(cfg.auto_update);
}

static void testSaveLoadRoundTrip() {
    Config in;
    in.model_path = "/opt/models/x.gguf";
    in.backend = "cpu";
    in.audio_device = "mic";
    in.history_count = 7;
    in.noise_threshold = 0.01f;
    in.overlay = false;
    in.trigger_key = "ctrl+shift+space";
    in.trigger_button = "L4";
    in.trigger_mode = "toggle";
    in.overlay_position = "bottom-right";
    in.submit_key = "tab";
    in.haptic = false;
    in.recording_timeout = 45;
    in.auto_update = false;
    in.pedals[0] = {PedalAction::Key, "ctrl+c", PedalMode::Tap, OutputMode::Type, "", "", ""};
    in.pedals[1] = {PedalAction::Transcribe, "", PedalMode::Hold, OutputMode::Clipboard, "enter", "", ""};
    in.pedals[2] = {PedalAction::Exec, "", PedalMode::Hold, OutputMode::Type, "", "echo a", "echo b"};

    std::string path = g_dir + "/sub/roundtrip.yaml";
    saveConfig(in, path);
    Config out = loadConfig(path);

    CHECK_EQ(out.model_path, in.model_path);
    CHECK_EQ(out.backend, in.backend);
    CHECK_EQ(out.audio_device, in.audio_device);
    CHECK_EQ(out.history_count, in.history_count);
    CHECK_EQ(out.noise_threshold, in.noise_threshold);
    CHECK_EQ(out.overlay, in.overlay);
    CHECK_EQ(out.trigger_key, in.trigger_key);
    CHECK_EQ(out.trigger_button, in.trigger_button);
    CHECK_EQ(out.trigger_mode, in.trigger_mode);
    CHECK_EQ(out.overlay_position, in.overlay_position);
    CHECK_EQ(out.submit_key, in.submit_key);
    CHECK_EQ(out.haptic, in.haptic);
    CHECK_EQ(out.recording_timeout, in.recording_timeout);
    CHECK_EQ(out.auto_update, in.auto_update);
    CHECK(out.pedals[0].action == PedalAction::Key);
    CHECK_EQ(out.pedals[0].key, "ctrl+c");
    CHECK(out.pedals[0].mode == PedalMode::Tap);
    CHECK(out.pedals[1].output == OutputMode::Clipboard);
    CHECK_EQ(out.pedals[1].suffix, "enter");
    CHECK_EQ(out.pedals[2].exec_press, "echo a");
    CHECK_EQ(out.pedals[2].exec_release, "echo b");
}

static void testParseChatProfiles() {
    Config none = loadConfig(writeFile("nochat.yaml", "submit_key: enter\n"));
    CHECK_EQ(none.chat_profile, "auto");
    CHECK(none.chat_profiles.empty());
    CHECK_EQ(loadConfig(writeFile("chatoff.yaml", "chat_profile: \"\"\n")).chat_profile, "");
    CHECK_EQ(loadConfig(writeFile("chatnone.yaml", "chat_profile: none\n")).chat_profile, "none");

    Config cfg = loadConfig(writeFile("chat.yaml",
        "chat_profile: ffxiv\n"
        "chat_profiles:\n"
        "  ffxiv:\n"
        "    open_key: space\n"
        "    match: ffxiv_dx11.exe, \"SquareEnix\\FINAL FANTASY XIV\"\n"
        "    channels:\n"
        "      fc:\n"
        "        command: \"/fc\"\n"
        "        say: [free company, \"fc\"]\n"
        "      tell:\n"
        "        command: /tell\n"
        "        say: tell, whisper\n"
        "        target: true\n"
        "    names:\n"
        "      y shtola: \"Y'shtola Rhul\"\n"
        "  wow:\n"
        "    names:\n"
        "      thrall: Thrall\n"
        "pedals:\n"
        "  center:\n"
        "    action: transcribe\n"));
    CHECK_EQ(cfg.chat_profile, "ffxiv");
    CHECK_EQ(cfg.chat_profiles.size(), 2u);
    const ChatProfile& ff = cfg.chat_profiles["ffxiv"];
    CHECK_EQ(ff.open_key, "space");
    CHECK_EQ(ff.match.size(), 2u);
    CHECK_EQ(ff.match[0], "ffxiv_dx11.exe");
    CHECK_EQ(ff.match[1], "SquareEnix\\FINAL FANTASY XIV");
    CHECK_EQ(ff.channels.size(), 2u);
    CHECK_EQ(ff.channels[0].name, "fc");
    CHECK_EQ(ff.channels[0].command, "/fc");
    CHECK_EQ(ff.channels[0].say.size(), 2u);
    CHECK_EQ(ff.channels[0].say[0], "free company");
    CHECK_EQ(ff.channels[0].say[1], "fc");
    CHECK(!ff.channels[0].target);
    CHECK_EQ(ff.channels[1].command, "/tell");
    CHECK(ff.channels[1].target);
    CHECK_EQ(ff.names.size(), 1u);
    CHECK_EQ(ff.names[0].first, "y shtola");
    CHECK_EQ(ff.names[0].second, "Y'shtola Rhul");
    CHECK_EQ(cfg.chat_profiles["wow"].names[0].second, "Thrall");
    CHECK(cfg.pedals[1].action == PedalAction::Transcribe);

    std::string path = g_dir + "/chat-roundtrip.yaml";
    saveConfig(cfg, path);
    Config out = loadConfig(path);
    CHECK_EQ(out.chat_profile, "ffxiv");
    CHECK_EQ(out.chat_profiles.size(), 2u);
    const ChatProfile& ff2 = out.chat_profiles["ffxiv"];
    CHECK_EQ(ff2.open_key, "space");
    CHECK_EQ(ff2.match.size(), 2u);
    CHECK_EQ(ff2.match[1], "SquareEnix\\FINAL FANTASY XIV");
    CHECK_EQ(out.chat_profiles.begin()->first, "ffxiv");
    CHECK_EQ(ff2.channels.size(), 2u);
    CHECK_EQ(ff2.channels[0].say.size(), 2u);
    CHECK_EQ(ff2.channels[0].say[0], "free company");
    CHECK_EQ(ff2.channels[1].command, "/tell");
    CHECK(ff2.channels[1].target);
    CHECK_EQ(ff2.names[0].second, "Y'shtola Rhul");
    CHECK_EQ(out.chat_profiles["wow"].names[0].first, "thrall");
    CHECK(out.pedals[1].action == PedalAction::Transcribe);

    saveConfig(Config{}, path);
    std::ifstream f(path);
    std::string saved((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(saved.find("chat_profile") == std::string::npos);

    Config off;
    off.chat_profile = "";
    saveConfig(off, path);
    CHECK_EQ(loadConfig(path).chat_profile, "none");
}

static void testEffectiveSubmitKey() {
    Config cfg;
    PedalConfig pedal;
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi"), "");

    pedal.suffix = "tab";
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi"), "tab");

    cfg.submit_key = "enter";
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi"), "enter");
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi\n"), "");
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, ""), "");

    cfg.submit_key = "tab";
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi\n"), "tab");

    cfg.submit_key = "none";
    pedal.suffix = "none";
    CHECK_EQ(effectiveSubmitKey(cfg, pedal, "hi"), "");
}

int main() {
    std::string tmpl = (std::filesystem::temp_directory_path() / "whisker-test-XXXXXX").string();
    g_dir = mkdtemp(tmpl.data());
    testMissingFileGivesDefaults();
    testExistingModelIsKept();
    testParseTopLevel();
    testParsePedals();
    testSaveLoadRoundTrip();
    testParseChatProfiles();
    testEffectiveSubmitKey();
    std::filesystem::remove_all(g_dir);
    return checkReport();
}

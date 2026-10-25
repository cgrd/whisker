// SPDX-License-Identifier: MIT
#include "check.h"
#include "config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        "recording_timeout: 60\n"));
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
    CHECK(out.pedals[0].action == PedalAction::Key);
    CHECK_EQ(out.pedals[0].key, "ctrl+c");
    CHECK(out.pedals[0].mode == PedalMode::Tap);
    CHECK(out.pedals[1].output == OutputMode::Clipboard);
    CHECK_EQ(out.pedals[1].suffix, "enter");
    CHECK_EQ(out.pedals[2].exec_press, "echo a");
    CHECK_EQ(out.pedals[2].exec_release, "echo b");
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
    testParseTopLevel();
    testParsePedals();
    testSaveLoadRoundTrip();
    testEffectiveSubmitKey();
    std::filesystem::remove_all(g_dir);
    return checkReport();
}

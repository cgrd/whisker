// SPDX-License-Identifier: MIT
#include "config.h"
#include "pedal.h"
#include "hotkey.h"
#include "gamepad.h"
#include "deckpad.h"
#include "keyboard.h"
#include "audio.h"
#include "audiocue.h"
#include "haptic.h"
#include "dictation.h"
#include "stt.h"
#include "overlay.h"
#include "history.h"
#include "clipboard.h"
#include "settings.h"
#include "tray.h"
#include "command.h"
#include "spawn.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <poll.h>
#include <sys/stat.h>

static volatile sig_atomic_t g_running = 1;

static void signalHandler(int) {
    g_running = 0;
}

static bool needsTranscribe(const Config& cfg) {
    if (!cfg.trigger_key.empty()) return true;
    if (!cfg.trigger_button.empty()) return true;
    for (auto& p : cfg.pedals)
        if (p.action == PedalAction::Transcribe) return true;
    return false;
}

static void outputText(const std::string& text, OutputMode mode, VirtualKeyboard& keyboard) {
    switch (mode) {
    case OutputMode::Type:
        keyboard.typeText(text);
        break;
    case OutputMode::Clipboard:
        clipboardCopy(text);
        break;
    case OutputMode::Both:
        clipboardCopy(text);
        keyboard.typeText(text);
        break;
    }
}

static int last_typed_len = 0;

static void doTranscribe(const PedalConfig& pcfg, AudioCapture& audio, SpeechToText& stt,
                         VirtualKeyboard& keyboard, Overlay& overlay, History& history,
                         Tray& tray, const Config& cfg, bool has_overlay,
                         Pedal* pedal, Hotkey* hotkey, Gamepad* gamepad,
                         AudioCue* cue, Haptic* haptic,
                         CommandSocket* cmdsock, Deckpad* deckpad,
                         PedalButton release_button) {
    fprintf(stderr, "whisker: recording...\n");
    audio.startCapture();
    if (has_overlay) overlay.show();
    if (cfg.tray) tray.setRecording(true);
    if (cue) cue->play(true);
    if (haptic) haptic->pulse(150, 0xC000);

    bool toggle = (cfg.trigger_mode == "toggle");

    struct pollfd fds[5];
    int nfds = 0;
    int pedal_idx = -1, hotkey_idx = -1, gamepad_idx = -1, cmd_idx = -1, deck_idx = -1;
    if (pedal && pedal->isOpen()) { pedal_idx = nfds; fds[nfds++] = {pedal->fd(), POLLIN, 0}; }
    if (hotkey && hotkey->fd() >= 0) { hotkey_idx = nfds; fds[nfds++] = {hotkey->fd(), POLLIN, 0}; }
    if (gamepad && gamepad->fd() >= 0) { gamepad_idx = nfds; fds[nfds++] = {gamepad->fd(), POLLIN, 0}; }
    if (cmdsock && cmdsock->fd() >= 0) { cmd_idx = nfds; fds[nfds++] = {cmdsock->fd(), POLLIN, 0}; }
    if (deckpad && deckpad->fd() >= 0) { deck_idx = nfds; fds[nfds++] = {deckpad->fd(), POLLIN, 0}; }

    auto rec_start = std::chrono::steady_clock::now();
    int timeout_ms = cfg.recording_timeout > 0 ? cfg.recording_timeout * 1000 : 0;

    bool released = false;
    while (g_running && !released) {
        if (timeout_ms > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - rec_start).count();
            if (elapsed >= timeout_ms) {
                fprintf(stderr, "whisker: recording timeout (%ds)\n", cfg.recording_timeout);
                break;
            }
        }
        int ret = ::poll(fds, nfds, 33);
        if (ret > 0) {
            if (pedal_idx >= 0 && (fds[pedal_idx].revents & POLLIN)) {
                PedalEvent ev;
                int pr = pedal->readEvent(ev);
                if (pr < 0) { released = true; break; }
                if (pr > 0 && ev.button == release_button && !ev.pressed)
                    released = true;
            }
            if (hotkey_idx >= 0 && (fds[hotkey_idx].revents & POLLIN)) {
                int hk = hotkey->readEvent();
                if (toggle ? (hk == 1) : (hk == -1)) released = true;
            }
            if (gamepad_idx >= 0 && (fds[gamepad_idx].revents & POLLIN)) {
                int gp = gamepad->readEvent();
                if (toggle ? (gp == 1) : (gp == -1)) released = true;
            }
            if (cmd_idx >= 0 && (fds[cmd_idx].revents & POLLIN)) {
                int cc = cmdsock->readCommand();
                if (cc == -1) released = true;
            }
            if (deck_idx >= 0 && (fds[deck_idx].revents & POLLIN)) {
                int dp = deckpad->readEvent();
                if (toggle ? (dp == 1) : (dp == -1)) released = true;
            }
        }
        if (cfg.tray) tray.poll();
        if (has_overlay) {
            auto recent = audio.peekRecent(4800);
            if (!recent.empty())
                overlay.updateWaveform(recent.data(), recent.size());
        }
    }

    auto samples = audio.stopCapture();
    if (cue) cue->play(false);
    if (haptic) {
        haptic->pulse(80, 0x8000);
        std::this_thread::sleep_for(std::chrono::milliseconds(160));
        haptic->pulse(80, 0x8000);
    }
    if (samples.empty()) {
        if (cfg.tray) tray.setRecording(false);
        if (has_overlay) overlay.hide();
        return;
    }

    if (cfg.undo_enabled && samples.size() < 4800 && last_typed_len > 0) {
        fprintf(stderr, "whisker: quick tap, undoing %d chars\n", last_typed_len);
        keyboard.emitBackspace(last_typed_len);
        last_typed_len = 0;
        if (cfg.tray) tray.setRecording(false);
        if (has_overlay) {
            overlay.updateText("(undo)");
            overlay.hideAfter(800);
        }
        return;
    }

    {
        double sum_sq = 0.0;
        for (float s : samples) sum_sq += s * s;
        float rms = static_cast<float>(std::sqrt(sum_sq / samples.size()));
        if (rms < cfg.noise_threshold) {
            fprintf(stderr, "whisker: below noise threshold (rms=%.6f), skipping\n", rms);
            if (cfg.tray) tray.setRecording(false);
            if (has_overlay) overlay.hide();
            return;
        }
    }

    float duration = static_cast<float>(samples.size()) / 16000.0f;
    fprintf(stderr, "whisker: captured %.1fs audio, transcribing...\n", duration);

    if (has_overlay) overlay.updateText("transcribing...");

    std::string text = stt.transcribe(samples);
    if (!text.empty()) {
        if (cfg.dictation_commands)
            text = processDictation(text);
        if (cfg.auto_capitalize)
            text = autoCapitalize(text);
        fprintf(stderr, "whisker: \"%s\"\n", text.c_str());
        if (has_overlay) overlay.updateText(text);
        outputText(text, pcfg.output, keyboard);
        last_typed_len = static_cast<int>(text.size());
        std::string submit = effectiveSubmitKey(cfg, pcfg, text);
        if (!submit.empty()) {
            auto combo = VirtualKeyboard::parseCombo(submit);
            if (!combo.empty()) {
                keyboard.emitCombo(combo, true);
                keyboard.emitCombo(combo, false);
            }
        }
        history.save(samples, text);
    }

    if (cfg.tray) tray.setRecording(false);
    if (has_overlay) {
        int words = 1;
        for (char c : text) if (c == ' ') ++words;
        int ms = std::max(1500, words * 200);
        overlay.hideAfter(ms);
    }
}

static bool comboValid(const char* what, const std::string& combo) {
    size_t parts = std::count(combo.begin(), combo.end(), '+') + 1;
    if (combo.empty() || VirtualKeyboard::parseCombo(combo).size() == parts)
        return true;
    fprintf(stderr, "whisker: %s: invalid key combo \"%s\"\n", what, combo.c_str());
    return false;
}

// Parses the config and key combos without touching any device, for CI and bug reports.
static int checkConfig(const std::string& path) {
    Config cfg = loadConfig(path);
    printf("config: %s\nmodel: %s\nbackend: %s\ntrigger_key: %s\ntrigger_button: %s\nsubmit_key: %s\n",
           path.c_str(), cfg.model_path.c_str(), cfg.backend.c_str(), cfg.trigger_key.c_str(),
           cfg.trigger_button.c_str(), cfg.submit_key.c_str());
    bool ok = comboValid("trigger_key", cfg.trigger_key);
    const char* names[] = {"pedals.left.key", "pedals.center.key", "pedals.right.key"};
    for (int i = 0; i < 3; ++i)
        if (cfg.pedals[i].action == PedalAction::Key)
            ok = comboValid(names[i], cfg.pedals[i].key) && ok;
    printf("%s\n", ok ? "ok" : "errors found");
    return ok ? 0 : 1;
}

static void printUsage() {
    printf("usage: whisker [-c CONFIG] [--settings] [--check-config] [--version]\n");
}

int main(int argc, char* argv[]) {
    std::string config_path = defaultConfigPath();
    bool open_settings = false;
    bool check_config = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--settings") == 0 || strcmp(argv[i], "-s") == 0) {
            open_settings = true;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--check-config") == 0) {
            check_config = true;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("whisker %s\n", WHISKER_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage();
            return 0;
        } else {
            fprintf(stderr, "whisker: unknown argument '%s'\n", argv[i]);
            printUsage();
            return 2;
        }
    }

    if (check_config)
        return checkConfig(config_path);

    if (!open_settings) {
        struct stat st;
        if (stat(config_path.c_str(), &st) != 0) {
            Config defaults = loadConfig(config_path);
            saveConfig(defaults, config_path);
        }
    }

    if (open_settings)
        return settingsRun(config_path);

    Config cfg = loadConfig(config_path);

    VirtualKeyboard keyboard;
    if (!keyboard.create()) {
        fprintf(stderr, "whisker: failed to create virtual keyboard\n");
        return 1;
    }

    AudioCapture audio;
    SpeechToText stt;
    bool has_transcribe = needsTranscribe(cfg);

    if (has_transcribe) {
        if (!audio.init()) {
            fprintf(stderr, "whisker: audio capture unavailable, continuing without transcription\n");
            has_transcribe = false;
        } else if (!stt.init(cfg.model_path, cfg.backend)) {
            fprintf(stderr, "whisker: no model loaded, continuing without transcription\n");
            has_transcribe = false;
        }
    }

    AudioCue audiocue;
    bool has_cue = has_transcribe && cfg.audio_cue;
    if (has_cue) {
        if (!audiocue.init()) {
            fprintf(stderr, "whisker: audio cue unavailable, continuing without\n");
            has_cue = false;
        }
    }

    Haptic haptic;
    bool has_haptic = cfg.haptic;
    if (has_haptic) {
        if (!haptic.init()) {
            fprintf(stderr, "whisker: haptic unavailable, continuing without\n");
            has_haptic = false;
        }
    }

    Overlay overlay;
    bool has_overlay = has_transcribe && cfg.overlay;
    if (has_overlay) {
        if (!overlay.init(cfg.overlay_position))
            fprintf(stderr, "whisker: overlay unavailable, continuing without\n");
    }

    Tray tray;
    if (cfg.tray) {
        if (!tray.init())
            fprintf(stderr, "whisker: tray unavailable, continuing without\n");
    }

    History history(cfg.history_count);

    std::vector<uint16_t> combos[3];
    for (int i = 0; i < 3; ++i) {
        if (cfg.pedals[i].action == PedalAction::Key)
            combos[i] = VirtualKeyboard::parseCombo(cfg.pedals[i].key);
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    CommandSocket cmdsock;
    std::string sock_path = std::string(getenv("XDG_RUNTIME_DIR") ?: "/tmp") + "/whisker.sock";
    bool has_cmdsock = cmdsock.init(sock_path);
    if (!has_cmdsock)
        fprintf(stderr, "whisker: command socket unavailable, continuing without\n");

    Pedal pedal;
    Hotkey hotkey;
    Gamepad gamepad;
    Deckpad deckpad;
    bool has_pedal = false;
    bool has_hotkey = false;
    bool has_gamepad = false;
    bool has_deckpad = false;

    if (!cfg.trigger_key.empty()) {
        has_hotkey = hotkey.init(cfg.trigger_key);
        if (!has_hotkey)
            fprintf(stderr, "whisker: failed to init hotkey\n");
    }

    if (Deckpad::handlesButton(cfg.trigger_button))
        has_deckpad = deckpad.init(cfg.trigger_button);

    if (!cfg.trigger_button.empty() && !has_deckpad) {
        has_gamepad = gamepad.init(cfg.trigger_button);
        if (!has_gamepad)
            fprintf(stderr, "whisker: failed to init gamepad trigger\n");
    }

    if (has_hotkey || has_gamepad || has_deckpad) {
        has_pedal = pedal.open();
        if (!has_pedal)
            fprintf(stderr, "whisker: no pedal found, using other triggers\n");
    } else {
        while (g_running && !pedal.open()) {
            fprintf(stderr, "whisker: waiting for pedal...\n");
            for (int i = 0; i < 20 && g_running; ++i) {
                if (cfg.tray) tray.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        has_pedal = g_running;
    }

    if (!has_pedal && !has_hotkey && !has_gamepad && !has_deckpad && !has_cmdsock) {
        fprintf(stderr, "whisker: no input method available\n");
        return 1;
    }

    fprintf(stderr, "whisker: ready\n");

    using clock = std::chrono::steady_clock;
    clock::time_point last_event_time[3] = {
        clock::time_point::min(),
        clock::time_point::min(),
        clock::time_point::min(),
    };

    auto& hotkey_pcfg = cfg.pedals[1];

    while (g_running) {
        if (cfg.tray) {
            tray.poll();
            if (tray.quitRequested()) {
                g_running = 0;
                continue;
            }
        }

        struct pollfd fds[5];
        int nfds = 0;
        int pedal_poll_idx = -1, hotkey_poll_idx = -1, gamepad_poll_idx = -1, cmd_poll_idx = -1;
        int deck_poll_idx = -1;

        if (has_pedal && pedal.isOpen()) {
            pedal_poll_idx = nfds;
            fds[nfds++] = {pedal.fd(), POLLIN, 0};
        }
        if (has_hotkey) {
            hotkey_poll_idx = nfds;
            fds[nfds++] = {hotkey.fd(), POLLIN, 0};
        }
        if (has_gamepad) {
            gamepad_poll_idx = nfds;
            fds[nfds++] = {gamepad.fd(), POLLIN, 0};
        }
        if (has_cmdsock) {
            cmd_poll_idx = nfds;
            fds[nfds++] = {cmdsock.fd(), POLLIN, 0};
        }
        if (has_deckpad) {
            deck_poll_idx = nfds;
            fds[nfds++] = {deckpad.fd(), POLLIN, 0};
        }

        if (nfds == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (has_pedal && !pedal.isOpen()) {
                if (pedal.open())
                    fprintf(stderr, "whisker: pedal reconnected\n");
            }
            continue;
        }

        int ret = ::poll(fds, nfds, 100);
        if (ret <= 0) {
            if (has_pedal && !pedal.isOpen()) {
                if (pedal.open())
                    fprintf(stderr, "whisker: pedal reconnected\n");
            }
            continue;
        }

        if (pedal_poll_idx >= 0 && (fds[pedal_poll_idx].revents & POLLIN)) {
            PedalEvent event;
            int pr = pedal.readEvent(event);
            if (pr < 0) {
                pedal.close();
                keyboard.releaseAll();
                fprintf(stderr, "whisker: pedal disconnected\n");
            } else if (pr > 0) {
                int idx = static_cast<int>(event.button);
                auto& pcfg = cfg.pedals[idx];
                auto now = clock::now();

                bool debounced = false;
                if (pcfg.action != PedalAction::Transcribe) {
                    if (now - last_event_time[idx] < std::chrono::milliseconds(800))
                        debounced = true;
                    else
                        last_event_time[idx] = now;
                }

                if (!debounced) {
                    switch (pcfg.action) {
                    case PedalAction::Key:
                        if (pcfg.mode == PedalMode::Hold) {
                            keyboard.emitCombo(combos[idx], event.pressed);
                        } else if (event.pressed) {
                            keyboard.emitCombo(combos[idx], true);
                            keyboard.emitCombo(combos[idx], false);
                        }
                        break;

                    case PedalAction::Transcribe:
                        if (event.pressed && has_transcribe) {
                            doTranscribe(pcfg, audio, stt, keyboard, overlay, history,
                                         tray, cfg, has_overlay,
                                         &pedal, has_hotkey ? &hotkey : nullptr,
                                         has_gamepad ? &gamepad : nullptr,
                                         has_cue ? &audiocue : nullptr,
                                         has_haptic ? &haptic : nullptr,
                                         has_cmdsock ? &cmdsock : nullptr,
                                         has_deckpad ? &deckpad : nullptr,
                                         event.button);
                        }
                        break;

                    case PedalAction::Exec:
                        if (event.pressed && !pcfg.exec_press.empty())
                            spawnDetached(pcfg.exec_press);
                        if (!event.pressed && !pcfg.exec_release.empty())
                            spawnDetached(pcfg.exec_release);
                        break;

                    case PedalAction::None:
                        break;
                    }
                }
            }
        }

        if (hotkey_poll_idx >= 0 && (fds[hotkey_poll_idx].revents & POLLIN)) {
            int hk = hotkey.readEvent();
            if (hk == 1 && has_transcribe) {
                doTranscribe(hotkey_pcfg, audio, stt, keyboard, overlay, history,
                             tray, cfg, has_overlay,
                             has_pedal ? &pedal : nullptr, &hotkey,
                             has_gamepad ? &gamepad : nullptr,
                             has_cue ? &audiocue : nullptr,
                             has_haptic ? &haptic : nullptr,
                             has_cmdsock ? &cmdsock : nullptr,
                             has_deckpad ? &deckpad : nullptr,
                             PedalButton::Center);
            }
        }

        if (gamepad_poll_idx >= 0 && (fds[gamepad_poll_idx].revents & POLLIN)) {
            int gp = gamepad.readEvent();
            if (gp == 1 && has_transcribe) {
                doTranscribe(hotkey_pcfg, audio, stt, keyboard, overlay, history,
                             tray, cfg, has_overlay,
                             has_pedal ? &pedal : nullptr,
                             has_hotkey ? &hotkey : nullptr,
                             &gamepad,
                             has_cue ? &audiocue : nullptr,
                             has_haptic ? &haptic : nullptr,
                             has_cmdsock ? &cmdsock : nullptr,
                             has_deckpad ? &deckpad : nullptr,
                             PedalButton::Center);
            }
        }

        if (cmd_poll_idx >= 0 && (fds[cmd_poll_idx].revents & POLLIN)) {
            int cc = cmdsock.readCommand();
            if (cc == 1 && has_transcribe) {
                doTranscribe(hotkey_pcfg, audio, stt, keyboard, overlay, history,
                             tray, cfg, has_overlay,
                             has_pedal ? &pedal : nullptr,
                             has_hotkey ? &hotkey : nullptr,
                             has_gamepad ? &gamepad : nullptr,
                             has_cue ? &audiocue : nullptr,
                             has_haptic ? &haptic : nullptr,
                             &cmdsock,
                             has_deckpad ? &deckpad : nullptr,
                             PedalButton::Center);
            }
        }

        if (deck_poll_idx >= 0 && (fds[deck_poll_idx].revents & POLLIN)) {
            int dp = deckpad.readEvent();
            if (dp == 1 && has_transcribe) {
                doTranscribe(hotkey_pcfg, audio, stt, keyboard, overlay, history,
                             tray, cfg, has_overlay,
                             has_pedal ? &pedal : nullptr,
                             has_hotkey ? &hotkey : nullptr,
                             has_gamepad ? &gamepad : nullptr,
                             has_cue ? &audiocue : nullptr,
                             has_haptic ? &haptic : nullptr,
                             has_cmdsock ? &cmdsock : nullptr,
                             &deckpad,
                             PedalButton::Center);
            }
        }
    }

    keyboard.releaseAll();
    hotkey.close();
    gamepad.close();
    deckpad.close();
    if (has_cmdsock) cmdsock.close();
    if (has_cue) audiocue.shutdown();
    if (has_haptic) haptic.shutdown();
    if (cfg.tray) tray.shutdown();
    if (has_overlay) overlay.shutdown();
    fprintf(stderr, "whisker: shutting down\n");
    return 0;
}

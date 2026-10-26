// SPDX-License-Identifier: MIT
#include "trigger_capture.h"
#include "buttons.h"

static std::string keyName(SDL_Scancode key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
        return std::string(1, static_cast<char>('a' + (key - SDL_SCANCODE_A)));
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9)
        return std::string(1, static_cast<char>('1' + (key - SDL_SCANCODE_1)));
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12)
        return "f" + std::to_string(1 + key - SDL_SCANCODE_F1);
    if (key >= SDL_SCANCODE_F13 && key <= SDL_SCANCODE_F24)
        return "f" + std::to_string(13 + key - SDL_SCANCODE_F13);
    switch (key) {
    case SDL_SCANCODE_0: return "0";
    case SDL_SCANCODE_SPACE: return "space";
    case SDL_SCANCODE_RETURN: return "enter";
    case SDL_SCANCODE_TAB: return "tab";
    case SDL_SCANCODE_ESCAPE: return "escape";
    case SDL_SCANCODE_BACKSPACE: return "backspace";
    case SDL_SCANCODE_DELETE: return "delete";
    case SDL_SCANCODE_UP: return "up";
    case SDL_SCANCODE_DOWN: return "down";
    case SDL_SCANCODE_LEFT: return "left";
    case SDL_SCANCODE_RIGHT: return "right";
    case SDL_SCANCODE_HOME: return "home";
    case SDL_SCANCODE_END: return "end";
    case SDL_SCANCODE_PAGEUP: return "pageup";
    case SDL_SCANCODE_PAGEDOWN: return "pagedown";
    case SDL_SCANCODE_INSERT: return "insert";
    case SDL_SCANCODE_AUDIOPLAY: return "playpause";
    case SDL_SCANCODE_AUDIONEXT: return "nextsong";
    case SDL_SCANCODE_AUDIOPREV: return "previoussong";
    case SDL_SCANCODE_VOLUMEUP: return "volumeup";
    case SDL_SCANCODE_VOLUMEDOWN: return "volumedown";
    case SDL_SCANCODE_MUTE:
    case SDL_SCANCODE_AUDIOMUTE: return "mute";
    default: return {};
    }
}

std::string modifierPrefix(Uint16 mod) {
    std::string out;
    if (mod & KMOD_CTRL) out += "ctrl+";
    if (mod & KMOD_SHIFT) out += "shift+";
    if (mod & KMOD_ALT) out += "alt+";
    if (mod & KMOD_GUI) out += "super+";
    return out;
}

std::string keyComboFromScancode(SDL_Scancode key, Uint16 mod) {
    std::string name = keyName(key);
    if (name.empty()) return {};
    return modifierPrefix(mod) + name;
}

CaptureReply parseCaptureReply(const std::string& line) {
    std::string text = line;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    if (text == "busy") return {{}, {}, "Whisker is recording, try again"};
    size_t space = text.find(' ');
    std::string chord = text.substr(0, space);
    if (chord.empty()) return {{}, {}, "No button pressed"};
    ButtonSet buttons = 0;
    if (!parseButtonChord(chord, buttons)) return {{}, {}, "Unknown button"};
    return {chord, space == std::string::npos ? std::string() : text.substr(space + 1), {}};
}

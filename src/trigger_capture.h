// SPDX-License-Identifier: MIT
#pragma once
#include <SDL_keycode.h>
#include <SDL_scancode.h>
#include <string>

// Config string like "ctrl+shift+space" for a physical key plus held modifiers, or "" when the
// key is a modifier itself or has no name in the hotkey parser.
std::string keyComboFromScancode(SDL_Scancode key, Uint16 mod);

// Names of the held modifiers, like "ctrl+shift+", shown while a capture waits for a key.
std::string modifierPrefix(Uint16 mod);

struct CaptureReply {
    std::string button;
    std::string device;
    std::string error;
};

// Parses the daemon's reply to the "capture" socket command.
CaptureReply parseCaptureReply(const std::string& line);

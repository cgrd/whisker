// SPDX-License-Identifier: MIT
#include "check.h"
#include "keyboard.h"
#include "trigger_capture.h"
#include <linux/input-event-codes.h>

using Codes = std::vector<uint16_t>;

static void checkRoundTrip(SDL_Scancode key, Uint16 mod, const std::string& combo, const Codes& codes) {
    std::string formatted = keyComboFromScancode(key, mod);
    CHECK_EQ(formatted, combo);
    CHECK(VirtualKeyboard::parseCombo(formatted) == codes);
}

static void testCombos() {
    checkRoundTrip(SDL_SCANCODE_SPACE, KMOD_LCTRL | KMOD_LSHIFT, "ctrl+shift+space",
                   {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_SPACE});
    checkRoundTrip(SDL_SCANCODE_F1, KMOD_RCTRL | KMOD_RSHIFT | KMOD_RALT, "ctrl+shift+alt+f1",
                   {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_F1});
    checkRoundTrip(SDL_SCANCODE_A, KMOD_LGUI, "super+a", {KEY_LEFTMETA, KEY_A});
    checkRoundTrip(SDL_SCANCODE_Z, KMOD_ALT, "alt+z", {KEY_LEFTALT, KEY_Z});
}

static void testSingleKeys() {
    checkRoundTrip(SDL_SCANCODE_F24, KMOD_NONE, "f24", {KEY_F24});
    checkRoundTrip(SDL_SCANCODE_F13, KMOD_NONE, "f13", {KEY_F13});
    checkRoundTrip(SDL_SCANCODE_0, KMOD_NONE, "0", {KEY_0});
    checkRoundTrip(SDL_SCANCODE_9, KMOD_NONE, "9", {KEY_9});
    checkRoundTrip(SDL_SCANCODE_RETURN, KMOD_NONE, "enter", {KEY_ENTER});
    checkRoundTrip(SDL_SCANCODE_PAGEDOWN, KMOD_NONE, "pagedown", {KEY_PAGEDOWN});
    checkRoundTrip(SDL_SCANCODE_AUDIOPLAY, KMOD_NONE, "playpause", {KEY_PLAYPAUSE});
    checkRoundTrip(SDL_SCANCODE_ESCAPE, KMOD_CTRL, "ctrl+escape", {KEY_LEFTCTRL, KEY_ESC});
}

static void testLocksIgnored() {
    CHECK_EQ(keyComboFromScancode(SDL_SCANCODE_SPACE, KMOD_NUM | KMOD_CAPS | KMOD_LCTRL), std::string("ctrl+space"));
}

static void testUnsupported() {
    CHECK_EQ(keyComboFromScancode(SDL_SCANCODE_LCTRL, KMOD_LCTRL), std::string());
    CHECK_EQ(keyComboFromScancode(SDL_SCANCODE_RSHIFT, KMOD_RSHIFT), std::string());
    CHECK_EQ(keyComboFromScancode(SDL_SCANCODE_KP_1, KMOD_NONE), std::string());
    CHECK_EQ(keyComboFromScancode(SDL_SCANCODE_SEMICOLON, KMOD_CTRL), std::string());
}

static void testModifierPrefix() {
    CHECK_EQ(modifierPrefix(KMOD_NONE), std::string());
    CHECK_EQ(modifierPrefix(KMOD_LSHIFT | KMOD_RGUI), std::string("shift+super+"));
}

static void testCaptureReply() {
    CaptureReply r = parseCaptureReply("L5 Steam Deck\n");
    CHECK_EQ(r.button, std::string("L5"));
    CHECK_EQ(r.device, std::string("Steam Deck"));
    CHECK(r.error.empty());

    r = parseCaptureReply("L3+R3 Xbox Wireless Controller\n");
    CHECK_EQ(r.button, std::string("L3+R3"));
    CHECK_EQ(r.device, std::string("Xbox Wireless Controller"));

    CHECK_EQ(parseCaptureReply("busy\n").error, std::string("Whisker is recording, try again"));
    CHECK_EQ(parseCaptureReply("\n").error, std::string("No button pressed"));
    CHECK_EQ(parseCaptureReply("").error, std::string("No button pressed"));
    CHECK_EQ(parseCaptureReply("Z9 Something\n").error, std::string("Unknown button"));
    CHECK(parseCaptureReply("Z9 Something\n").button.empty());
}

int main() {
    testCombos();
    testSingleKeys();
    testLocksIgnored();
    testUnsupported();
    testModifierPrefix();
    testCaptureReply();
    return checkReport();
}

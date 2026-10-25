// SPDX-License-Identifier: MIT
#include "check.h"
#include "keyboard.h"
#include <linux/input-event-codes.h>

using Codes = std::vector<uint16_t>;

static void testCombos() {
    CHECK(VirtualKeyboard::parseCombo("ctrl+shift+space") == Codes{KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_SPACE});
    CHECK(VirtualKeyboard::parseCombo("ctrl+shift+alt+F1") ==
          Codes{KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_F1});
    CHECK(VirtualKeyboard::parseCombo("super+a") == Codes{KEY_LEFTMETA, KEY_A});
}

static void testSingleKeys() {
    CHECK(VirtualKeyboard::parseCombo("enter") == Codes{KEY_ENTER});
    CHECK(VirtualKeyboard::parseCombo("tab") == Codes{KEY_TAB});
    CHECK(VirtualKeyboard::parseCombo("esc") == Codes{KEY_ESC});
    CHECK(VirtualKeyboard::parseCombo("f24") == Codes{KEY_F24});
}

static void testWhitespaceAndCase() {
    CHECK(VirtualKeyboard::parseCombo(" Ctrl + Enter ") == Codes{KEY_LEFTCTRL, KEY_ENTER});
    CHECK(VirtualKeyboard::parseCombo("CONTROL+Z") == Codes{KEY_LEFTCTRL, KEY_Z});
}

static void testInvalid() {
    CHECK(VirtualKeyboard::parseCombo("").empty());
    CHECK(VirtualKeyboard::parseCombo("bogus").empty());
    CHECK(VirtualKeyboard::parseCombo("ctrl+bogus") == Codes{KEY_LEFTCTRL});
    CHECK(VirtualKeyboard::parseCombo("none").empty());
}

int main() {
    testCombos();
    testSingleKeys();
    testWhitespaceAndCase();
    testInvalid();
    return checkReport();
}

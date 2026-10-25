// SPDX-License-Identifier: MIT
#include "check.h"
#include "gamepad.h"
#include <vector>

using Codes = std::vector<uint16_t>;

static constexpr uint16_t kValve = 0x28DE;
static constexpr uint16_t kMicrosoft = 0x045E;
static constexpr uint16_t kSony = 0x054C;

static void testBackButtonsUseGripCodes() {
    CHECK(gamepadButtonCodes("L4", kSony) == Codes{0x224});
    CHECK(gamepadButtonCodes("R4", kSony) == Codes{0x225});
    CHECK(gamepadButtonCodes("L5", kSony) == Codes{0x226});
    CHECK(gamepadButtonCodes("R5", kSony) == Codes{0x227});
}

static void testLegacyDeckCodes() {
    CHECK(gamepadButtonCodes("L4", kValve) == Codes{0x224, 0x2c0});
    CHECK(gamepadButtonCodes("R4", kValve) == Codes{0x225, 0x2c1});
    CHECK(gamepadButtonCodes("L5", kValve) == Codes{0x226, 0x2c2});
    CHECK(gamepadButtonCodes("R5", kValve) == Codes{0x227, 0x2c3});
}

static void testLegacyElitePaddleCodes() {
    CHECK(gamepadButtonCodes("R4", kMicrosoft) == Codes{0x225, 0x2c4});
    CHECK(gamepadButtonCodes("R5", kMicrosoft) == Codes{0x227, 0x2c5});
    CHECK(gamepadButtonCodes("L4", kMicrosoft) == Codes{0x224, 0x2c6});
    CHECK(gamepadButtonCodes("L5", kMicrosoft) == Codes{0x226, 0x2c7});
}

static void testStandardButtons() {
    CHECK(gamepadButtonCodes("A", kSony) == Codes{0x130});
    CHECK(gamepadButtonCodes("guide", kValve) == Codes{0x13c});
    CHECK(gamepadButtonCodes("704", 0) == Codes{704});
    CHECK(gamepadButtonCodes("L6", 0).empty());
    CHECK(gamepadButtonCodes("", 0).empty());
    CHECK(gamepadButtonCodes("70000", 0).empty());
}

int main() {
    testBackButtonsUseGripCodes();
    testLegacyDeckCodes();
    testLegacyElitePaddleCodes();
    testStandardButtons();
    return checkReport();
}

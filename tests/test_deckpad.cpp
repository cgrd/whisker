// SPDX-License-Identifier: MIT
#include "check.h"
#include "deckpad.h"
#include <array>

using Report = std::array<uint8_t, 64>;

static Report inputReport() {
    Report r{};
    r[0] = 0x01;
    r[2] = 0x09;
    return r;
}

static int state(const Report& r, const char* button) {
    return deckButtonState(r.data(), r.size(), button);
}

static void testIdleReport() {
    Report r = inputReport();
    for (const char* b : {"L4", "R4", "L5", "R5"})
        CHECK_EQ(state(r, b), 0);
}

static void testEachButtonBit() {
    struct Case { const char* button; int byte; uint8_t mask; };
    const Case cases[] = {
        {"L5", 9, 0x80},
        {"R5", 10, 0x01},
        {"L4", 13, 0x02},
        {"R4", 13, 0x04},
    };
    for (const auto& c : cases) {
        Report r = inputReport();
        r[c.byte] = c.mask;
        for (const auto& other : cases) {
            bool sameBit = other.byte == c.byte && other.mask == c.mask;
            CHECK_EQ(state(r, other.button), sameBit ? 1 : 0);
        }
    }
}

static void testAllPressed() {
    Report r = inputReport();
    r[9] = 0xff;
    r[10] = 0xff;
    r[13] = 0xff;
    for (const char* b : {"L4", "R4", "L5", "R5"})
        CHECK_EQ(state(r, b), 1);
}

static void testRejectsOtherReports() {
    Report r = inputReport();
    r[9] = 0x80;
    Report wrongId = r;
    wrongId[0] = 0x02;
    CHECK_EQ(state(wrongId, "L5"), -1);
    Report wrongType = r;
    wrongType[2] = 0x04;
    CHECK_EQ(state(wrongType, "L5"), -1);
    CHECK_EQ(deckButtonState(r.data(), 63, "L5"), -1);
}

static void testUnknownButton() {
    Report r = inputReport();
    CHECK_EQ(state(r, "select"), -1);
    CHECK_EQ(state(r, ""), -1);
    CHECK(Deckpad::handlesButton("L5"));
    CHECK(Deckpad::handlesButton("R4"));
    CHECK(!Deckpad::handlesButton("select"));
    CHECK(!Deckpad::handlesButton("l5"));
}

int main() {
    testIdleReport();
    testEachButtonBit();
    testAllPressed();
    testRejectsOtherReports();
    testUnknownButton();
    return checkReport();
}

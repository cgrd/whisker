// SPDX-License-Identifier: MIT
#include "check.h"
#include "buttons.h"
#include <linux/input.h>
#include <string>
#include <vector>

using Report = std::vector<uint8_t>;

static constexpr uint16_t kDeck = 0x1205;
static constexpr uint16_t kPuck = 0x1304;
static constexpr uint16_t kSteamController2015 = 0x1102;

static Report deckReport() {
    Report r(64, 0);
    r[0] = 0x01;
    r[2] = 0x09;
    r[3] = 0x40;
    return r;
}

// 2026 Steam Controller state report 0x42 as captured from the puck: 54 bytes, buttons LE32 at 2.
static Report tritonReport(uint8_t id = 0x42, size_t len = 54) {
    Report r(len, 0);
    r[0] = id;
    r[1] = 0x17;
    return r;
}

static void setBit(Report& r, int offset, int bit) {
    r[offset + bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
}

static ButtonSet decode(uint16_t product, const Report& r) {
    ButtonSet b = 0xFFFFFFFF;
    CHECK(decodeValveReport(product, r.data(), r.size(), b));
    return b;
}

static bool rejected(uint16_t product, const Report& r) {
    ButtonSet b;
    return !decodeValveReport(product, r.data(), r.size(), b);
}

static void testDeckBits() {
    CHECK_EQ(decode(kDeck, deckReport()), 0u);
    struct Case { Button button; int byte; uint8_t mask; };
    const Case cases[] = {
        {kButtonL5, 9, 0x80}, {kButtonR5, 10, 0x01}, {kButtonL4, 13, 0x02}, {kButtonR4, 13, 0x04},
        {kButtonA, 8, 0x80}, {kButtonL3, 10, 0x40}, {kButtonR3, 11, 0x04}, {kButtonStart, 9, 0x40},
    };
    for (const auto& c : cases) {
        Report r = deckReport();
        r[c.byte] = c.mask;
        CHECK_EQ(decode(kDeck, r), ButtonSet(c.button));
    }
}

static void testDeckRejectsOtherReports() {
    Report r = deckReport();
    r[9] = 0x80;
    Report wrong_id = r;
    wrong_id[0] = 0x02;
    CHECK(rejected(kDeck, wrong_id));
    Report wrong_type = r;
    wrong_type[2] = 0x04;
    CHECK(rejected(kDeck, wrong_type));
    CHECK(rejected(kDeck, Report(r.begin(), r.begin() + 63)));
}

static void testTritonBits() {
    CHECK_EQ(decode(kPuck, tritonReport()), 0u);
    struct Case { Button button; int bit; };
    const Case cases[] = {
        {kButtonA, 0}, {kButtonR4, 7}, {kButtonR5, 8}, {kButtonL4, 17}, {kButtonL5, 18},
    };
    for (const auto& c : cases) {
        Report r = tritonReport();
        setBit(r, 2, c.bit);
        CHECK_EQ(decode(kPuck, r), ButtonSet(c.button));
    }
}

static void testTritonIgnoresGripTouch() {
    Report r = tritonReport();
    setBit(r, 2, 28);
    setBit(r, 2, 29);
    CHECK_EQ(decode(kPuck, r), 0u);
    setBit(r, 2, 18);
    CHECK_EQ(decode(kPuck, r), ButtonSet(kButtonL5));
}

static void testTritonReportKinds() {
    Report ble = tritonReport(0x45, 46);
    setBit(ble, 2, 18);
    CHECK_EQ(decode(0x1303, ble), ButtonSet(kButtonL5));
    CHECK_EQ(decode(0x1302, ble), ButtonSet(kButtonL5));
    CHECK_EQ(decode(0x1305, ble), ButtonSet(kButtonL5));

    for (uint8_t id : {0x43, 0x46, 0x79, 0x7b}) {
        Report r = tritonReport(id);
        setBit(r, 2, 18);
        CHECK(rejected(kPuck, r));
    }
    CHECK(rejected(kPuck, tritonReport(0x42, 45)));
    CHECK(rejected(kDeck, tritonReport()));
}

static void testSteamController2015Bits() {
    Report r = deckReport();
    r[2] = 0x01;
    CHECK_EQ(decode(kSteamController2015, r), 0u);
    r[9] = 0x80;
    r[10] = 0x01;
    CHECK_EQ(decode(kSteamController2015, r), ButtonSet(kButtonL5 | kButtonR5));
    CHECK_EQ(decode(0x1142, r), ButtonSet(kButtonL5 | kButtonR5));
    CHECK(rejected(kSteamController2015, deckReport()));
}

static void testUnknownProduct() {
    CHECK(rejected(0x11FF, deckReport()));
    CHECK(valveFamily(0x11FF) == nullptr);
    CHECK(valveFamily(kPuck)->tested);
    CHECK(!valveFamily(kSteamController2015)->tested);
}

static ButtonSet chord(const std::string& text) {
    ButtonSet b = 0;
    CHECK(parseButtonChord(text, b));
    return b;
}

static bool invalidChord(const std::string& text) {
    ButtonSet b;
    return !parseButtonChord(text, b);
}

static void testChordParsing() {
    CHECK_EQ(chord("L5"), ButtonSet(kButtonL5));
    CHECK_EQ(chord("L3+R3"), ButtonSet(kButtonL3 | kButtonR3));
    CHECK_EQ(chord("L1 + R1"), ButtonSet(kButtonL1 | kButtonR1));
    CHECK_EQ(chord("select+up"), ButtonSet(kButtonSelect | kButtonUp));
    CHECK(invalidChord(""));
    CHECK(invalidChord("l5"));
    CHECK(invalidChord("L6"));
    CHECK(invalidChord("L3+"));
    CHECK(invalidChord("L3+L3"));
    CHECK(invalidChord("704"));
}

static void testChordFormatting() {
    CHECK_EQ(formatButtonChord(kButtonL5), std::string("L5"));
    CHECK_EQ(formatButtonChord(kButtonR3 | kButtonL3), std::string("L3+R3"));
    CHECK_EQ(formatButtonChord(0), std::string(""));
    for (const char* text : {"A", "L1+R1", "L4+R4", "select+start", "guide"})
        CHECK_EQ(formatButtonChord(chord(text)), std::string(text));
}

static void testChordTransitions() {
    ButtonSet c = kButtonL3 | kButtonR3;
    CHECK_EQ(chordTransition(0, kButtonL3, c), 0);
    CHECK_EQ(chordTransition(kButtonL3, c, c), 1);
    CHECK_EQ(chordTransition(c, c | kButtonA, c), 0);
    CHECK_EQ(chordTransition(c, kButtonR3, c), -1);
    CHECK_EQ(chordTransition(kButtonR3, 0, c), 0);
    CHECK_EQ(chordTransition(0, kButtonL5, kButtonL5), 1);
    CHECK_EQ(chordTransition(kButtonL5, 0, kButtonL5), -1);
    CHECK_EQ(chordTransition(0, kButtonL5, 0), 0);
}

static void testCapture() {
    ChordCapture single;
    CHECK(!single.update(kButtonL5));
    CHECK(single.update(0));
    CHECK_EQ(single.peak, ButtonSet(kButtonL5));

    ChordCapture combo;
    CHECK(!combo.update(kButtonL3));
    CHECK(!combo.update(kButtonL3 | kButtonR3));
    CHECK(!combo.update(kButtonR3));
    CHECK(combo.update(0));
    CHECK_EQ(combo.peak, ButtonSet(kButtonL3 | kButtonR3));

    ChordCapture held_at_start;
    held_at_start.armed = false;
    CHECK(!held_at_start.update(kButtonA | kButtonL5));
    CHECK(!held_at_start.update(0));
    CHECK(!held_at_start.update(kButtonR4));
    CHECK(held_at_start.update(0));
    CHECK_EQ(held_at_start.peak, ButtonSet(kButtonR4));
}

static void testHidId() {
    uint16_t v = 0, p = 0;
    CHECK(parseHidId("DRIVER=steam\nHID_ID=0003:000028DE:00001304\nHID_NAME=Valve Software Steam Controller\n", v, p));
    CHECK_EQ(v, 0x28DE);
    CHECK_EQ(p, 0x1304);
    CHECK(parseHidId("HID_ID=0005:000028DE:00001303\n", v, p));
    CHECK_EQ(p, 0x1303);
    CHECK(!parseHidId("DRIVER=steam\n", v, p));
}

static ButtonSet evdev(ButtonSet held, uint16_t type, uint16_t code, int32_t value, uint16_t vendor = 0x054C) {
    return applyEvdevEvent(held, type, code, value, vendor);
}

static void testEvdevKeys() {
    CHECK_EQ(evdev(0, EV_KEY, BTN_SOUTH, 1), ButtonSet(kButtonA));
    CHECK_EQ(evdev(kButtonA, EV_KEY, BTN_SOUTH, 0), 0u);
    CHECK_EQ(evdev(kButtonA, EV_KEY, BTN_SOUTH, 2), ButtonSet(kButtonA));
    CHECK_EQ(evdev(0, EV_KEY, BTN_MODE, 1), ButtonSet(kButtonGuide));
    CHECK_EQ(evdev(0, EV_KEY, BTN_THUMBR, 1), ButtonSet(kButtonR3));
    CHECK_EQ(evdev(0, EV_KEY, 0x224, 1), ButtonSet(kButtonL4));
    CHECK_EQ(evdev(0, EV_KEY, 0x225, 1), ButtonSet(kButtonR4));
    CHECK_EQ(evdev(0, EV_KEY, 0x226, 1), ButtonSet(kButtonL5));
    CHECK_EQ(evdev(0, EV_KEY, 0x227, 1), ButtonSet(kButtonR5));
    CHECK_EQ(evdev(0, EV_KEY, KEY_A, 1), 0u);
    CHECK_EQ(evdev(0, EV_REL, REL_X, 1), 0u);
}

static void testLegacyElitePaddles() {
    constexpr uint16_t kMicrosoft = 0x045E;
    CHECK_EQ(evdev(0, EV_KEY, BTN_TRIGGER_HAPPY5, 1, kMicrosoft), ButtonSet(kButtonR4));
    CHECK_EQ(evdev(0, EV_KEY, BTN_TRIGGER_HAPPY6, 1, kMicrosoft), ButtonSet(kButtonR5));
    CHECK_EQ(evdev(0, EV_KEY, BTN_TRIGGER_HAPPY7, 1, kMicrosoft), ButtonSet(kButtonL4));
    CHECK_EQ(evdev(0, EV_KEY, BTN_TRIGGER_HAPPY8, 1, kMicrosoft), ButtonSet(kButtonL5));
    CHECK_EQ(evdev(0, EV_KEY, BTN_TRIGGER_HAPPY5, 1), 0u);
}

static void testEvdevHat() {
    ButtonSet b = evdev(0, EV_ABS, ABS_HAT0X, -1);
    CHECK_EQ(b, ButtonSet(kButtonLeft));
    b = evdev(b, EV_ABS, ABS_HAT0X, 1);
    CHECK_EQ(b, ButtonSet(kButtonRight));
    b = evdev(b, EV_ABS, ABS_HAT0Y, -1);
    CHECK_EQ(b, ButtonSet(kButtonRight | kButtonUp));
    b = evdev(b, EV_ABS, ABS_HAT0X, 0);
    CHECK_EQ(b, ButtonSet(kButtonUp));
    CHECK_EQ(evdev(kButtonA, EV_ABS, ABS_X, 100), ButtonSet(kButtonA));
}

int main() {
    testDeckBits();
    testDeckRejectsOtherReports();
    testTritonBits();
    testTritonIgnoresGripTouch();
    testTritonReportKinds();
    testSteamController2015Bits();
    testUnknownProduct();
    testChordParsing();
    testChordFormatting();
    testChordTransitions();
    testCapture();
    testHidId();
    testEvdevKeys();
    testLegacyElitePaddles();
    testEvdevHat();
    return checkReport();
}

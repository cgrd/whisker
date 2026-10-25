// SPDX-License-Identifier: MIT
#include "buttons.h"
#include <linux/input.h>
#include <cstdio>
#include <cstdlib>

// Kernel headers before 6.17 lack these.
#ifndef BTN_GRIPL
#define BTN_GRIPL  0x224
#define BTN_GRIPR  0x225
#define BTN_GRIPL2 0x226
#define BTN_GRIPR2 0x227
#endif

namespace {

struct Name {
    Button button;
    const char* name;
};

constexpr Name kNames[] = {
    {kButtonA, "A"}, {kButtonB, "B"}, {kButtonX, "X"}, {kButtonY, "Y"},
    {kButtonL1, "L1"}, {kButtonR1, "R1"}, {kButtonL2, "L2"}, {kButtonR2, "R2"},
    {kButtonL3, "L3"}, {kButtonR3, "R3"},
    {kButtonL4, "L4"}, {kButtonL5, "L5"}, {kButtonR4, "R4"}, {kButtonR5, "R5"},
    {kButtonSelect, "select"}, {kButtonStart, "start"}, {kButtonGuide, "guide"},
    {kButtonUp, "up"}, {kButtonDown, "down"}, {kButtonLeft, "left"}, {kButtonRight, "right"},
};

struct BitMap {
    Button button;
    uint8_t bit;
};

// Steam Deck: little-endian u64 at byte 8 of report type 0x09 (hid-steam steam_deck_button_mappings).
constexpr BitMap kDeckBits[] = {
    {kButtonR2, 0}, {kButtonL2, 1}, {kButtonR1, 2}, {kButtonL1, 3},
    {kButtonY, 4}, {kButtonB, 5}, {kButtonX, 6}, {kButtonA, 7},
    {kButtonUp, 8}, {kButtonRight, 9}, {kButtonLeft, 10}, {kButtonDown, 11},
    {kButtonSelect, 12}, {kButtonGuide, 13}, {kButtonStart, 14},
    {kButtonL5, 15}, {kButtonR5, 16}, {kButtonL3, 22}, {kButtonR3, 26},
    {kButtonL4, 41}, {kButtonR4, 42},
};

// 2015 Steam Controller: same header and lower bits as the Deck, report type 0x01. Its two
// back levers are Steam's button_back_left/right, so L5/R5. The right pad click is not R3.
constexpr BitMap kSteamController2015Bits[] = {
    {kButtonR2, 0}, {kButtonL2, 1}, {kButtonR1, 2}, {kButtonL1, 3},
    {kButtonY, 4}, {kButtonB, 5}, {kButtonX, 6}, {kButtonA, 7},
    {kButtonUp, 8}, {kButtonRight, 9}, {kButtonLeft, 10}, {kButtonDown, 11},
    {kButtonSelect, 12}, {kButtonGuide, 13}, {kButtonStart, 14},
    {kButtonL5, 15}, {kButtonR5, 16}, {kButtonL3, 22},
};

// 2026 Steam Controller: little-endian u32 at byte 2 (SDL_hidapi_steam_triton.c). Bits 28 and
// 29 are grip touch sensors and stay unmapped. Select/start follow SDL's mapping of MENU/VIEW.
constexpr BitMap kTritonBits[] = {
    {kButtonA, 0}, {kButtonB, 1}, {kButtonX, 2}, {kButtonY, 3},
    {kButtonR3, 5}, {kButtonStart, 6}, {kButtonR4, 7}, {kButtonR5, 8}, {kButtonR1, 9},
    {kButtonDown, 10}, {kButtonRight, 11}, {kButtonLeft, 12}, {kButtonUp, 13},
    {kButtonSelect, 14}, {kButtonL3, 15}, {kButtonGuide, 16}, {kButtonL4, 17},
    {kButtonL5, 18}, {kButtonL1, 19}, {kButtonR2, 23}, {kButtonL2, 27},
};

constexpr size_t kDeckReportLen = 64;
// The smallest Triton state report (0x45 over BLE); 0x42 is 54 bytes.
constexpr size_t kTritonMinReportLen = 46;

constexpr ValveFamily kFamilies[] = {
    {0x1205, "Steam Deck", true},
    {0x1102, "Steam Controller (2015, USB)", false},
    {0x1142, "Steam Controller (2015, dongle)", false},
    {0x1302, "Steam Controller (USB)", false},
    {0x1303, "Steam Controller (Bluetooth)", false},
    {0x1304, "Steam Controller (puck)", true},
    {0x1305, "Steam Controller (Steam Machine)", false},
};

uint64_t readLE(const uint8_t* p, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

template <size_t N>
ButtonSet mapBits(uint64_t word, const BitMap (&table)[N]) {
    ButtonSet out = 0;
    for (const auto& m : table)
        if ((word >> m.bit) & 1) out |= m.button;
    return out;
}

bool isValveHeader(const uint8_t* data, size_t len, uint8_t type) {
    return len == kDeckReportLen && data[0] == 0x01 && data[1] == 0x00 && data[2] == type;
}

bool isTritonState(uint8_t id) {
    return id == 0x42 || id == 0x45 || id == 0x47;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

Button lookupName(const std::string& name) {
    for (const auto& n : kNames)
        if (name == n.name) return n.button;
    return Button(0);
}

constexpr uint16_t kMicrosoftVendor = 0x045E;

Button evdevKey(uint16_t code, uint16_t vendor) {
    switch (code) {
    case BTN_SOUTH: return kButtonA;
    case BTN_EAST: return kButtonB;
    case BTN_WEST: return kButtonX;
    case BTN_NORTH: return kButtonY;
    case BTN_TL: return kButtonL1;
    case BTN_TR: return kButtonR1;
    case BTN_TL2: return kButtonL2;
    case BTN_TR2: return kButtonR2;
    case BTN_THUMBL: return kButtonL3;
    case BTN_THUMBR: return kButtonR3;
    case BTN_SELECT: return kButtonSelect;
    case BTN_START: return kButtonStart;
    case BTN_MODE: return kButtonGuide;
    case BTN_DPAD_UP: return kButtonUp;
    case BTN_DPAD_DOWN: return kButtonDown;
    case BTN_DPAD_LEFT: return kButtonLeft;
    case BTN_DPAD_RIGHT: return kButtonRight;
    case BTN_GRIPL: return kButtonL4;
    case BTN_GRIPR: return kButtonR4;
    case BTN_GRIPL2: return kButtonL5;
    case BTN_GRIPR2: return kButtonR5;
    }
    // Before Linux 6.17 xpad reported the Elite paddles as BTN_TRIGGER_HAPPY5-8, which other
    // drivers use for unrelated buttons.
    if (vendor == kMicrosoftVendor) {
        switch (code) {
        case BTN_TRIGGER_HAPPY5: return kButtonR4;
        case BTN_TRIGGER_HAPPY6: return kButtonR5;
        case BTN_TRIGGER_HAPPY7: return kButtonL4;
        case BTN_TRIGGER_HAPPY8: return kButtonL5;
        }
    }
    return Button(0);
}

ButtonSet applyHat(ButtonSet held, ButtonSet negative, ButtonSet positive, int32_t value) {
    held &= ~(negative | positive);
    if (value < 0) held |= negative;
    if (value > 0) held |= positive;
    return held;
}

} // namespace

bool parseButtonChord(const std::string& text, ButtonSet& out) {
    ButtonSet chord = 0;
    size_t start = 0;
    while (true) {
        size_t plus = text.find('+', start);
        Button b = lookupName(trim(text.substr(start, plus - start)));
        if (b == 0 || (chord & b)) return false;
        chord |= b;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    out = chord;
    return true;
}

std::string formatButtonChord(ButtonSet buttons) {
    std::string out;
    for (const auto& n : kNames) {
        if (!(buttons & n.button)) continue;
        if (!out.empty()) out += '+';
        out += n.name;
    }
    return out;
}

int chordTransition(ButtonSet before, ButtonSet after, ButtonSet chord) {
    if (chord == 0) return 0;
    bool was = (before & chord) == chord;
    bool is = (after & chord) == chord;
    if (is && !was) return 1;
    if (was && !is) return -1;
    return 0;
}

bool ChordCapture::update(ButtonSet held) {
    if (!armed) {
        armed = held == 0;
        return false;
    }
    peak |= held;
    return peak != 0 && held == 0;
}

const ValveFamily* valveFamily(uint16_t product) {
    for (const auto& f : kFamilies)
        if (f.product == product) return &f;
    return nullptr;
}

bool decodeValveReport(uint16_t product, const uint8_t* data, size_t len, ButtonSet& out) {
    switch (product) {
    case 0x1205:
        if (!isValveHeader(data, len, 0x09)) return false;
        out = mapBits(readLE(data + 8, 8), kDeckBits);
        return true;
    case 0x1102:
    case 0x1142:
        if (!isValveHeader(data, len, 0x01)) return false;
        out = mapBits(readLE(data + 8, 8), kSteamController2015Bits);
        return true;
    case 0x1302:
    case 0x1303:
    case 0x1304:
    case 0x1305:
        if (len < kTritonMinReportLen || !isTritonState(data[0])) return false;
        out = mapBits(readLE(data + 2, 4), kTritonBits);
        return true;
    }
    return false;
}

bool parseHidId(const std::string& uevent, uint16_t& vendor, uint16_t& product) {
    size_t pos = uevent.find("HID_ID=");
    if (pos == std::string::npos) return false;
    unsigned bus, v, p;
    if (sscanf(uevent.c_str() + pos, "HID_ID=%x:%x:%x", &bus, &v, &p) != 3) return false;
    if (v > 0xFFFF || p > 0xFFFF) return false;
    vendor = static_cast<uint16_t>(v);
    product = static_cast<uint16_t>(p);
    return true;
}

ButtonSet applyEvdevEvent(ButtonSet held, uint16_t type, uint16_t code, int32_t value, uint16_t vendor) {
    if (type == EV_KEY) {
        Button b = evdevKey(code, vendor);
        if (b == 0) return held;
        return value ? (held | b) : (held & ~ButtonSet(b));
    }
    if (type == EV_ABS && code == ABS_HAT0X) return applyHat(held, kButtonLeft, kButtonRight, value);
    if (type == EV_ABS && code == ABS_HAT0Y) return applyHat(held, kButtonUp, kButtonDown, value);
    return held;
}

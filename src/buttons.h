// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Device-neutral buttons. L4/R4 are the upper back buttons and L5/R5 the lower ones on
// every device, matching Steam's names (button_back_left_upper = L4, button_back_left = L5).
enum Button : uint32_t {
    kButtonA = 1u << 0,
    kButtonB = 1u << 1,
    kButtonX = 1u << 2,
    kButtonY = 1u << 3,
    kButtonL1 = 1u << 4,
    kButtonR1 = 1u << 5,
    kButtonL2 = 1u << 6,
    kButtonR2 = 1u << 7,
    kButtonL3 = 1u << 8,
    kButtonR3 = 1u << 9,
    kButtonSelect = 1u << 10,
    kButtonStart = 1u << 11,
    kButtonGuide = 1u << 12,
    kButtonL4 = 1u << 13,
    kButtonL5 = 1u << 14,
    kButtonR4 = 1u << 15,
    kButtonR5 = 1u << 16,
    kButtonUp = 1u << 17,
    kButtonDown = 1u << 18,
    kButtonLeft = 1u << 19,
    kButtonRight = 1u << 20,
};

using ButtonSet = uint32_t;

// "L5" or a chord like "L3+R3"; false for empty, unknown or repeated names.
bool parseButtonChord(const std::string& text, ButtonSet& out);
std::string formatButtonChord(ButtonSet buttons);

// 1 when the chord became fully held, -1 when a held chord lost a button, else 0.
int chordTransition(ButtonSet before, ButtonSet after, ButtonSet chord);

// Collects everything held during one gesture; done once it was pressed and fully released.
// Not armed while buttons from before the capture (like the A press that started it) are held.
struct ChordCapture {
    bool armed = true;
    ButtonSet peak = 0;
    bool update(ButtonSet held);
};

struct ValveFamily {
    uint16_t product;
    const char* name;
    bool tested;
};

// nullptr for products Whisker has no decoder for.
const ValveFamily* valveFamily(uint16_t product);

// false if the report isn't a controller state report for this product.
bool decodeValveReport(uint16_t product, const uint8_t* data, size_t len, ButtonSet& out);

// Parses HID_ID=bus:vendor:product from a hidraw device's uevent text.
bool parseHidId(const std::string& uevent, uint16_t& vendor, uint16_t& product);

// Applies one evdev event to the held buttons.
ButtonSet applyEvdevEvent(ButtonSet held, uint16_t type, uint16_t code, int32_t value, uint16_t vendor);

// SPDX-License-Identifier: MIT
#include "check.h"
#include "haptic.h"

static InputDevice physicalPad() {
    InputDevice d;
    d.sysfs_path = "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/input/input20";
    d.vendor = 0x045E;
    d.product = 0x0B12;
    d.rumble = RumbleKind::Evdev;
    return d;
}

static InputDevice steamVirtualPad() {
    InputDevice d;
    d.sysfs_path = "/sys/devices/virtual/input/input37";
    d.vendor = 0x28DE;
    d.product = 0x11FF;
    return d;
}

static InputDevice valveHidraw(uint16_t product, bool streaming) {
    InputDevice d;
    d.hidraw = true;
    d.streaming = streaming;
    d.vendor = 0x28DE;
    d.product = product;
    d.rumble = hidrawRumbleKind(product);
    return d;
}

static void testVirtualDetection() {
    CHECK(!isVirtualInput(physicalPad()));
    CHECK(isVirtualInput(steamVirtualPad()));

    InputDevice uinput = physicalPad();
    uinput.sysfs_path = "/sys/devices/virtual/input/input50";
    CHECK(isVirtualInput(uinput));

    InputDevice steam_pad_elsewhere = steamVirtualPad();
    steam_pad_elsewhere.sysfs_path = "/sys/devices/pci0000:00/input/input9";
    CHECK(isVirtualInput(steam_pad_elsewhere));
}

static void testRumbleKinds() {
    CHECK(hidrawRumbleKind(0x1205) == RumbleKind::DeckFeature);
    CHECK(hidrawRumbleKind(0x1302) == RumbleKind::TritonOutput);
    CHECK(hidrawRumbleKind(0x1303) == RumbleKind::TritonOutput);
    CHECK(hidrawRumbleKind(0x1304) == RumbleKind::TritonOutput);
    CHECK(hidrawRumbleKind(0x1305) == RumbleKind::TritonOutput);
    CHECK(hidrawRumbleKind(0x1102) == RumbleKind::None);
}

static void testTargetChoice() {
    InputDevice no_rumble = physicalPad();
    no_rumble.rumble = RumbleKind::None;
    std::vector<InputDevice> devices = {
        valveHidraw(0x1205, false),
        valveHidraw(0x1205, true),
        physicalPad(),
        no_rumble,
        valveHidraw(0x1304, true),
        valveHidraw(0x1102, true),
    };

    CHECK_EQ(chooseHapticDevice(devices, 2), 2);
    CHECK_EQ(chooseHapticDevice(devices, 4), 4);
    CHECK_EQ(chooseHapticDevice(devices, 1), 1);
    CHECK_EQ(chooseHapticDevice(devices, 3), 1);
    CHECK_EQ(chooseHapticDevice(devices, 5), 1);
    CHECK_EQ(chooseHapticDevice(devices, -1), 1);

    std::vector<InputDevice> desktop = {no_rumble, valveHidraw(0x1205, false)};
    CHECK_EQ(chooseHapticDevice(desktop, 0), -1);
    CHECK_EQ(chooseHapticDevice(desktop, -1), -1);
    CHECK_EQ(chooseHapticDevice({}, -1), -1);
}

static void testDeckRumbleReport() {
    DeckFeatureReport r = deckRumbleReport(0xC000, 0x6000);
    CHECK_EQ(int(r[0]), 0x00);
    CHECK_EQ(int(r[1]), 0xEB);
    CHECK_EQ(int(r[2]), 9);
    CHECK_EQ(int(r[4]), 0);
    CHECK_EQ(int(r[5]), 0);
    CHECK_EQ(int(r[6]), 0x00);
    CHECK_EQ(int(r[7]), 0xC0);
    CHECK_EQ(int(r[8]), 0x00);
    CHECK_EQ(int(r[9]), 0x60);
    CHECK_EQ(int(r[10]), 2);
    CHECK_EQ(int(r[11]), 0);

    DeckFeatureReport stop = deckRumbleReport(0, 0);
    for (size_t i = 3; i < stop.size(); ++i)
        if (i != 10) CHECK_EQ(int(stop[i]), 0);
}

static void testTritonRumbleReport() {
    TritonRumbleReport r = tritonRumbleReport(0xC000, 0x6000);
    const int expected[] = {0x80, 0, 0, 0, 0x00, 0xC0, 0, 0x00, 0x60, 0};
    for (size_t i = 0; i < r.size(); ++i)
        CHECK_EQ(int(r[i]), expected[i]);

    TritonRumbleReport stop = tritonRumbleReport(0, 0);
    CHECK_EQ(int(stop[0]), 0x80);
    for (size_t i = 1; i < stop.size(); ++i)
        CHECK_EQ(int(stop[i]), 0);
}

int main() {
    testVirtualDetection();
    testRumbleKinds();
    testTargetChoice();
    testDeckRumbleReport();
    testTritonRumbleReport();
    return checkReport();
}

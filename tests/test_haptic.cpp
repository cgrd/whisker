// SPDX-License-Identifier: MIT
#include "check.h"
#include "haptic.h"

static InputDeviceInfo physicalPad() {
    return {"/sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/input/input20", 0x045E, 0x0B12, true};
}

static InputDeviceInfo steamVirtualPad() {
    return {"/sys/devices/virtual/input/input37", 0x28DE, 0x11FF, true};
}

static void testVirtualDetection() {
    CHECK(!isVirtualInput(physicalPad()));
    CHECK(isVirtualInput(steamVirtualPad()));

    InputDeviceInfo uinput = physicalPad();
    uinput.sysfs_path = "/sys/devices/virtual/input/input50";
    CHECK(isVirtualInput(uinput));

    InputDeviceInfo steam_pad_elsewhere = steamVirtualPad();
    steam_pad_elsewhere.sysfs_path = "/sys/devices/pci0000:00/input/input9";
    CHECK(isVirtualInput(steam_pad_elsewhere));
}

static void testTargetChoice() {
    InputDeviceInfo pad = physicalPad();
    InputDeviceInfo virt = steamVirtualPad();
    InputDeviceInfo no_rumble = physicalPad();
    no_rumble.rumble = false;

    CHECK(chooseHapticTarget(&pad, true) == HapticTarget::TriggerPad);
    CHECK(chooseHapticTarget(&pad, false) == HapticTarget::TriggerPad);
    CHECK(chooseHapticTarget(&virt, true) == HapticTarget::DeckController);
    CHECK(chooseHapticTarget(&virt, false) == HapticTarget::None);
    CHECK(chooseHapticTarget(&no_rumble, false) == HapticTarget::None);
    CHECK(chooseHapticTarget(nullptr, true) == HapticTarget::DeckController);
    CHECK(chooseHapticTarget(nullptr, false) == HapticTarget::None);
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

int main() {
    testVirtualDetection();
    testTargetChoice();
    testDeckRumbleReport();
    return checkReport();
}

# Adding a controller

Whisker reads Valve controllers (Steam Deck, Steam Controller) from raw HID reports, because Steam hides their evdev nodes in Game Mode. Everything else goes through evdev and the kernel driver's button codes, so most gamepads need no work here. A new decoder is needed when a Valve device has an unknown product ID or report layout, or when a driver reports a button under an unusual evdev code.

## 1. Capture the reports

Connect the controller, keep Steam running if that's how you'll use it, and run:

```sh
python3 scripts/controller-capture.py
```

It lists every hidraw node (vendor:product, bus, USB interface, name) and evdev gamepad, then calibrates for 3 seconds. Keep your hands off the controller so the sticks, motion sensors, sequence numbers and touch sensors get masked. Then press and release one button at a time. Each line shows the report ID and the bits that changed:

```
/dev/hidraw5 report 0x42 byte4.bit2(bit34)=1
/dev/hidraw5 report 0x42 byte4.bit2(bit34)=0
```

`bitN` counts from the first byte of the report. For a little-endian button word starting at byte 2, subtract 16 to get the word bit (bit 34 is word bit 18, L5 on the Steam Controller). Add `--all` to watch non-Valve hidraw nodes too. For evdev gamepads it prints the key codes (`EV_KEY code=0x226 value=1`).

Note which hidraw node streams (a wireless receiver has one per controller slot), the report ID and length, and the bit of every button. Bits that flip without a button press (grip touch, for example) must stay unmapped.

## 2. Add a fixture and a test

In `tests/test_buttons.cpp`, build a report from your capture (ID, length, the bit set for one button) and check that `decodeValveReport` returns exactly that button, ignores the noisy bits and rejects other report IDs. Follow `testTritonBits` and `testTritonReportKinds`.

## 3. Add the table entry

In `src/buttons.cpp`:

* add the product to `kFamilies`, with `tested` set to `true` only if the bits come from a capture on real hardware
* add a `BitMap` table that maps each bit to a device-neutral button, using Steam's names by position (L4/R4 upper back buttons, L5/R5 lower)
* add a case to `decodeValveReport` that checks the report ID and minimum length and reads the button word

If the device can rumble, add its product to `hidrawRumbleKind` in `src/haptic.cpp` with a report builder and a test in `tests/test_haptic.cpp`.

For an evdev controller with unusual codes, extend `evdevKey` in `src/buttons.cpp`, limited to the driver's vendor ID if the codes mean something else on other devices. The Xbox Elite paddles (Microsoft, `BTN_TRIGGER_HAPPY5-8` before Linux 6.17) and the Stadia controller's Assistant and Capture buttons (Google, `BTN_TRIGGER_HAPPY1-2`) work this way. A new button name goes into `Button` in `src/buttons.h`, `kNames` in `src/buttons.cpp`, `BUTTONS` in `decky/main.py`, the Decky button options in `decky/src/index.tsx` and the button list in the README.

Run `make check` and `make hil-test`, then add the controller to the table in the README. For a Valve device whose reports differ from the 2026 Steam Controller, give `scripts/hil-test.py` a fake for it too. Paste the capture output in the PR.

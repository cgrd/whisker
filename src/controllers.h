// SPDX-License-Identifier: MIT
#pragma once
#include "buttons.h"
#include "haptic.h"
#include <csignal>
#include <functional>
#include <string>
#include <vector>

// Every connected controller: Valve devices over hidraw (Steam hides their evdev nodes in
// Game Mode), everything else over evdev. One epoll fd covers all devices and hotplug.
class Controllers {
public:
    // Opens all controllers if a trigger is set; an invalid trigger is logged and ignored.
    bool init(const std::string& trigger);
    void close();
    int fd() const { return epoll_fd_; }
    bool hasTrigger() const { return trigger_ != 0; }

    // 1=trigger chord pressed, -1=released, 0=no change. Also handles hotplug.
    int readEvent();
    // Path of the device that last pressed the trigger.
    const std::string& lastTrigger() const { return last_trigger_; }

    // Opens all controllers, then waits for one button or chord pressed and released on any device.
    // Returns "<chord> <device name>" or "" on timeout.
    std::string capture(int timeout_ms, const volatile sig_atomic_t& running);

    // Rumbles the device at trigger_path, or the Deck's controller if that can't rumble.
    void pulse(const std::string& trigger_path, int duration_ms, int strength);

private:
    int epoll_fd_ = -1;
    int inotify_fd_ = -1;
    bool opened_ = false;
    ButtonSet trigger_ = 0;
    std::string last_trigger_;
    std::vector<InputDevice> devices_;

    void openAll();
    void scan();
    void openNode(const std::string& path);
    void removeDevice(int fd);
    void handleHotplug();
    int findByFd(int fd) const;
    bool isOpen(const std::string& path) const;
    // Calls on_change for every button change until it returns true; true if it did.
    bool drain(const std::function<bool(InputDevice&, ButtonSet)>& on_change);
    bool readDevice(InputDevice& dev, const std::function<bool(InputDevice&, ButtonSet)>& on_change);
};

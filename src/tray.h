// SPDX-License-Identifier: MIT
#pragma once
#include <string>

class Tray {
public:
    bool init();
    void shutdown();
    void setRecording(bool recording);
    void poll();
    bool quitRequested() const;
};

// Shows a desktop notification through org.freedesktop.Notifications. Returns false if there is
// no session bus or notification server.
bool notifyDesktop(const std::string& summary, const std::string& body);

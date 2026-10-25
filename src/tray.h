// SPDX-License-Identifier: MIT
#pragma once

class Tray {
public:
    bool init();
    void shutdown();
    void setRecording(bool recording);
    void poll();
    bool quitRequested() const;
};

// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct PillRect {
    double x, y;
    int w, h;
};

using OverlayPainter = std::function<void(unsigned char* data, int width, int height,
                                          int stride, int scale, const PillRect& pill)>;

class OverlayBackend {
public:
    virtual ~OverlayBackend() = default;

    virtual const char* name() const = 0;
    virtual bool ok() const = 0;
    virtual int outputWidth() const = 0;

    virtual void show(int pill_w, int pill_h) = 0;
    virtual void hide() = 0;
    virtual void render(int pill_w, int pill_h, const OverlayPainter& paint) = 0;
    virtual void dispatch(int timeout_ms) = 0;
};

bool gamescopeRunning();
// $WAYLAND_DISPLAY first, then other wayland-* sockets in $XDG_RUNTIME_DIR
std::vector<std::string> waylandDisplayNames();
std::unique_ptr<OverlayBackend> createGamescopeBackend(const std::string& position);
std::unique_ptr<OverlayBackend> createLayerShellBackend(const std::string& position);

// SPDX-License-Identifier: MIT
#pragma once

#include <string>

struct SDL_Renderer;

struct SettingsRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct SettingsLayout {
    SettingsRect content;
    SettingsRect overlay_checkbox;
    SettingsRect trigger_input;
    SettingsRect save_button;
    bool save_hovered = false;
    bool font_texture_ready = false;
};

int settingsRun(const std::string& config_path);

// Draws the settings UI into `renderer` the way settingsRun does, with the window size and
// framebuffer scale the SDL platform backend would report, so tests can run it headless.
SettingsLayout settingsRenderOffscreen(SDL_Renderer* renderer, const std::string& config_path,
                                       float width, float height, float framebuffer_scale,
                                       float mouse_x, float mouse_y, int frames);

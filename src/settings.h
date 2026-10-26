// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <optional>
#include <string>

struct SDL_Renderer;

struct SettingsRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

enum class SettingsTab { Audio, Model, Triggers, History };

struct SettingsLayout {
    SettingsRect content;
    SettingsRect overlay_checkbox;
    SettingsRect model_prompt;
    SettingsRect model_link;
    bool model_link_hovered = false;
    SettingsRect save_button;
    bool save_hovered = false;
    bool font_texture_ready = false;
    // Interactive items on the drawn page by name, and the one under the mouse.
    std::map<std::string, SettingsRect> items;
    std::string hovered;
    SettingsTab active_tab = SettingsTab::Audio;
};

int settingsRun(const std::string& config_path);

// Draws the settings UI into `renderer` the way settingsRun does, with the window size and
// framebuffer scale the SDL platform backend would report, so tests can run it headless.
SettingsLayout settingsRenderOffscreen(SDL_Renderer* renderer, const std::string& config_path,
                                       float width, float height, float framebuffer_scale,
                                       float mouse_x, float mouse_y, int frames,
                                       std::optional<SettingsTab> tab = std::nullopt);

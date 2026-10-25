// SPDX-License-Identifier: MIT
#include "check.h"
#include "settings.h"
#include <SDL.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static const float kWidth = 1200.0f;
static const float kHeight = 850.0f;

static bool inside(const SettingsRect& inner, const SettingsRect& outer) {
    return inner.x0 >= outer.x0 && inner.y0 >= outer.y0 && inner.x1 <= outer.x1 && inner.y1 <= outer.y1 &&
           inner.x1 > inner.x0 && inner.y1 > inner.y0;
}

static SDL_Color pixelAt(SDL_Surface* surface, float x, float y) {
    int px = static_cast<int>(x);
    int py = static_cast<int>(y);
    auto* row = static_cast<Uint8*>(surface->pixels) + py * surface->pitch;
    Uint32 value = reinterpret_cast<Uint32*>(row)[px];
    SDL_Color c{};
    SDL_GetRGB(value, surface->format, &c.r, &c.g, &c.b);
    return c;
}

static void checkScale(const std::string& config, float scale) {
    std::printf("framebuffer scale %.2f\n", scale);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, static_cast<int>(std::lround(kWidth * scale)), static_cast<int>(std::lround(kHeight * scale)),
        32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer != nullptr);

    SettingsLayout probe = settingsRenderOffscreen(renderer, config, kWidth, kHeight, scale, -1, -1, 2);
    float save_x = (probe.save_button.x0 + probe.save_button.x1) / 2;
    float save_y = (probe.save_button.y0 + probe.save_button.y1) / 2;

    SettingsLayout layout = settingsRenderOffscreen(renderer, config, kWidth, kHeight, scale, save_x, save_y, 3);
    SettingsRect display{0, 0, kWidth, kHeight};

    CHECK(layout.font_texture_ready);
    CHECK(inside(layout.content, display));
    CHECK(layout.content.x1 > kWidth - 32);
    CHECK(inside(layout.overlay_checkbox, layout.content));
    CHECK(inside(layout.trigger_input, layout.content));
    CHECK(inside(layout.save_button, display));
    CHECK(layout.save_hovered);

    // The pixel under the mouse, in framebuffer coordinates, must show the hovered Save button.
    SDL_Color save = pixelAt(surface, save_x * scale, save_y * scale);
    std::printf("save pixel %d,%d,%d\n", save.r, save.g, save.b);
    CHECK(save.b > 150 && save.b > save.r + 100);

    // Checkbox frames are clipped away when clip rects and vertices disagree on the scale.
    float checkbox_y = (layout.overlay_checkbox.y0 + layout.overlay_checkbox.y1) / 2 * scale;
    SDL_Color frame = pixelAt(surface, (layout.overlay_checkbox.x0 + 4) * scale, checkbox_y);
    SDL_Color child_bg = pixelAt(surface, (layout.overlay_checkbox.x0 - 4) * scale, checkbox_y);
    std::printf("checkbox pixel %d,%d,%d next to it %d,%d,%d\n", frame.r, frame.g, frame.b,
                child_bg.r, child_bg.g, child_bg.b);
    CHECK(frame.r != child_bg.r || frame.g != child_bg.g || frame.b != child_bg.b);

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
}

int main() {
    fs::path home = fs::temp_directory_path() / ("whisker-test-settings-" + std::to_string(getpid()));
    fs::create_directories(home);
    setenv("HOME", home.c_str(), 1);

    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f})
        checkScale(SAMPLE_CONFIG, scale);

    fs::remove_all(home);
    return checkReport();
}

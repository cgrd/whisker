// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

class OverlayBackend;
struct PillRect;

class Overlay {
public:
    Overlay();
    ~Overlay();

    bool init(const std::string& position = "bottom");
    void shutdown();

    void show();
    void hide();
    void hideAfter(int ms);

    void updateWaveform(const float* samples, size_t count);
    void updateText(const std::string& text);

private:
    std::thread render_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> visible_{false};
    std::atomic<int64_t> hide_at_ms_{0};

    std::unique_ptr<OverlayBackend> backend_;
    std::string position_ = "bottom";
    int32_t pill_w_ = 400;
    int32_t pill_h_ = 44;

    FT_Library ft_library_ = nullptr;
    FT_Face ft_face_ = nullptr;
    struct _cairo_font_face* font_face_ = nullptr;

    std::mutex data_mutex_;
    std::vector<float> waveform_samples_;
    size_t waveform_write_pos_ = 0;
    std::string text_;

    void renderLoop();
    void selectBackend();
    void drawFrame(unsigned char* data, int width, int height, int stride,
                   int scale, const PillRect& pill);
    void recalcPillHeight();
};

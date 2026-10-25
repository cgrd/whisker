// SPDX-License-Identifier: MIT
#include "overlay.h"

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include "font_embed.h"
#include "overlay_backend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>

static constexpr int SPECTRUM_BINS = 64;
static constexpr int WAVEFORM_COLS = 400;
static constexpr int WAVEFORM_HEIGHT = 32;
static constexpr int TEXT_Y = 38;
static constexpr int PADDING = 12;

static void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

Overlay::Overlay() {
    waveform_samples_.resize(WAVEFORM_COLS, 0.0f);
}

Overlay::~Overlay() {
    shutdown();
}

bool Overlay::init(const std::string& position) {
    position_ = position;

    // Xlib writes to its socket without MSG_NOSIGNAL; a vanished X server must not kill the daemon.
    signal(SIGPIPE, SIG_IGN);

    if (FT_Init_FreeType(&ft_library_) == 0 &&
        FT_New_Memory_Face(ft_library_, AtkinsonHyperlegible_Regular_ttf,
                           AtkinsonHyperlegible_Regular_ttf_len, 0, &ft_face_) == 0)
        font_face_ = cairo_ft_font_face_create_for_ft_face(ft_face_, 0);

    running_.store(true);
    render_thread_ = std::thread(&Overlay::renderLoop, this);
    return true;
}

void Overlay::shutdown() {
    running_.store(false);
    if (render_thread_.joinable())
        render_thread_.join();

    if (font_face_) { cairo_font_face_destroy(font_face_); font_face_ = nullptr; }
    if (ft_face_) { FT_Done_Face(ft_face_); ft_face_ = nullptr; }
    if (ft_library_) { FT_Done_FreeType(ft_library_); ft_library_ = nullptr; }
}

void Overlay::show() {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        std::fill(waveform_samples_.begin(), waveform_samples_.end(), 0.0f);
        waveform_write_pos_ = 0;
        text_.clear();
        recalcPillHeight();
    }
    hide_at_ms_.store(0);
    visible_.store(true);
}

void Overlay::hide() {
    hide_at_ms_.store(0);
    visible_.store(false);
}

void Overlay::hideAfter(int ms) {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(ms);
    hide_at_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline.time_since_epoch()).count());
}

static void fft_radix2(float* re, float* im, size_t n) {
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * M_PI / len;
        float wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cur_r = 1.0f, cur_i = 0.0f;
            for (size_t j = 0; j < len / 2; j++) {
                float tr = re[i + j + len / 2] * cur_r - im[i + j + len / 2] * cur_i;
                float ti = re[i + j + len / 2] * cur_i + im[i + j + len / 2] * cur_r;
                re[i + j + len / 2] = re[i + j] - tr;
                im[i + j + len / 2] = im[i + j] - ti;
                re[i + j] += tr;
                im[i + j] += ti;
                float new_r = cur_r * wr - cur_i * wi;
                cur_i = cur_r * wi + cur_i * wr;
                cur_r = new_r;
            }
        }
    }
}

void Overlay::updateWaveform(const float* samples, size_t count) {
    if (!count) return;

    constexpr size_t FFT_N = 1024;
    size_t n = std::min(count, FFT_N);
    size_t offset = (count > FFT_N) ? count - FFT_N : 0;

    float re[FFT_N] = {}, im[FFT_N] = {};
    for (size_t j = 0; j < n; j++) {
        float w = 0.5f - 0.5f * std::cos(2.0f * M_PI * j / (n - 1));
        re[j] = samples[offset + j] * w;
    }

    fft_radix2(re, im, FFT_N);

    float mags[FFT_N / 2];
    for (size_t k = 0; k < FFT_N / 2; k++)
        mags[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]) / FFT_N;

    float bins[SPECTRUM_BINS] = {};
    float freq_min = 1.0f, freq_max = FFT_N / 2.0f;
    for (int b = 0; b < SPECTRUM_BINS; b++) {
        float f0 = freq_min * std::pow(freq_max / freq_min, (float)b / SPECTRUM_BINS);
        float f1 = freq_min * std::pow(freq_max / freq_min, (float)(b + 1) / SPECTRUM_BINS);
        int k0 = std::max(0, static_cast<int>(f0));
        int k1 = std::min(static_cast<int>(FFT_N / 2 - 1), static_cast<int>(f1));
        float peak = 0.0f;
        for (int k = k0; k <= k1; k++)
            peak = std::max(peak, mags[k]);
        bins[b] = peak;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);
    for (int b = 0; b < SPECTRUM_BINS; b++) {
        float target = std::min(1.0f, bins[b] * 80.0f);
        float prev = (b < static_cast<int>(waveform_samples_.size())) ? waveform_samples_[b] : 0.0f;
        waveform_samples_[b] = (target > prev) ? target : prev * 0.85f + target * 0.15f;
    }
    waveform_write_pos_ = SPECTRUM_BINS;
}

void Overlay::updateText(const std::string& text) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    text_ = text;
    if (!text.empty())
        recalcPillHeight();
}

void Overlay::recalcPillHeight() {
    int new_h = WAVEFORM_HEIGHT + PADDING;
    if (!text_.empty()) {
        int lines = 1;
        double pill_r = (WAVEFORM_HEIGHT + PADDING) / 2.0;
        double max_w = pill_w_ - 2 * (pill_r + 4.0);
        cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cairo_t* cr = cairo_create(tmp);
        if (font_face_) {
            cairo_set_font_face(cr, font_face_);
            cairo_set_font_size(cr, 13.0);
        } else {
            cairo_set_font_size(cr, 13.0);
        }
        std::string line;
        size_t pos = 0;
        while (pos <= text_.size()) {
            size_t space = text_.find(' ', pos);
            if (space == std::string::npos) space = text_.size();
            std::string word = text_.substr(pos, space - pos);
            std::string candidate = line.empty() ? word : line + " " + word;
            cairo_text_extents_t ext;
            cairo_text_extents(cr, candidate.c_str(), &ext);
            if (ext.width > max_w && !line.empty()) {
                lines++;
                line = word;
            } else {
                line = candidate;
            }
            pos = space + 1;
        }
        cairo_destroy(cr);
        cairo_surface_destroy(tmp);
        new_h = WAVEFORM_HEIGHT + 8 + lines * 16 + 8;
    }
    pill_h_ = std::min(new_h, 200);
}

void Overlay::selectBackend() {
    bool gamescope = gamescopeRunning();
    const char* wanted = gamescope ? "gamescope" : "layer-shell";
    if (backend_ && backend_->ok() && strcmp(backend_->name(), wanted) == 0)
        return;

    backend_.reset();
    if (gamescope)
        backend_ = createGamescopeBackend(position_);
    if (!backend_)
        backend_ = createLayerShellBackend(position_);
    fprintf(stderr, "overlay: selected %s backend\n", backend_ ? backend_->name() : "no");
}

void Overlay::renderLoop() {
    OverlayPainter paint = [this](unsigned char* data, int w, int h, int stride,
                                  int scale, const PillRect& pill) {
        drawFrame(data, w, h, stride, scale, pill);
    };
    auto pillSize = [this] {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return std::pair{pill_w_, pill_h_};
    };

    bool active = false;
    while (running_.load()) {
        bool want_visible = visible_.load();

        if (want_visible && !active) {
            selectBackend();
            if (backend_) {
                {
                    std::lock_guard<std::mutex> lock(data_mutex_);
                    int out_w = backend_->outputWidth();
                    pill_w_ = out_w > 0 ? std::clamp(out_w / 5, 240, 500) : 400;
                    recalcPillHeight();
                }
                auto [w, h] = pillSize();
                backend_->show(w, h);
            }
            active = true;
        } else if (!want_visible && active) {
            if (backend_)
                backend_->hide();
            active = false;
        }

        int timeout_ms = want_visible ? 33 : 100;
        if (backend_)
            backend_->dispatch(timeout_ms);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));

        if (backend_ && active && backend_->ok()) {
            auto [w, h] = pillSize();
            backend_->render(w, h, paint);
        }

        if (backend_ && !backend_->ok()) {
            fprintf(stderr, "overlay: lost %s display connection\n", backend_->name());
            backend_.reset();
        }

        int64_t deadline = hide_at_ms_.load();
        if (deadline > 0) {
            using clock = std::chrono::steady_clock;
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                clock::now().time_since_epoch()).count();
            if (now_ms >= deadline) {
                hide_at_ms_.store(0);
                visible_.store(false);
            }
        }
    }

    if (backend_ && active)
        backend_->hide();
    backend_.reset();
}

void Overlay::drawFrame(unsigned char* data, int width, int height, int stride,
                        int scale, const PillRect& pill) {
    cairo_surface_t* cs = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, width, height, stride);
    cairo_t* cr = cairo_create(cs);

    if (scale > 1)
        cairo_scale(cr, scale, scale);

    int pw = pill.w;
    int ph = pill.h;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_save(cr);
    cairo_translate(cr, pill.x, pill.y);

    double pill_r = ph / 2.0;
    roundedRect(cr, 0, 0, pw, ph, pill_r);
    cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, ph);
    cairo_pattern_add_color_stop_rgba(bg, 0.0, 0.08, 0.08, 0.14, 0.92);
    cairo_pattern_add_color_stop_rgba(bg, 1.0, 0.06, 0.06, 0.11, 0.92);
    cairo_set_source(cr, bg);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(bg);

    cairo_set_source_rgba(cr, 0.35, 0.55, 0.85, 0.25);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);

    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        constexpr double min_bar_h = 2.0;
        double h_pad = pill_r + 4.0;
        double wave_area_w = pw - 2.0 * h_pad;
        int num_bars = std::min(SPECTRUM_BINS, static_cast<int>(waveform_samples_.size()));
        double bar_step = wave_area_w / num_bars;
        double bar_width = std::max(2.0, bar_step - 2.0);
        bool has_text = !text_.empty();
        double wave_y_center = has_text ? WAVEFORM_HEIGHT / 2.0 : ph / 2.0;
        double max_half = (has_text ? WAVEFORM_HEIGHT / 2.0 : ph / 2.0) - 3.0;

        cairo_pattern_t* grad = cairo_pattern_create_linear(0, 0, pw, 0);
        cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.0, 0.824, 1.0, 0.9);
        cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.227, 0.482, 0.835, 0.9);
        cairo_set_source(cr, grad);

        for (int b = 0; b < num_bars; b++) {
            float amp = waveform_samples_[b];
            double half_h = std::max(min_bar_h, amp * max_half);
            double x = h_pad + b * bar_step + (bar_step - bar_width) / 2.0;
            double r = bar_width / 2.0;

            double top = wave_y_center - half_h;
            double bot = wave_y_center + half_h;
            double h = bot - top;

            if (h < bar_width) {
                cairo_arc(cr, x + r, wave_y_center, r, 0, 2 * M_PI);
            } else {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x + r, top + r, r, M_PI, 2 * M_PI);
                cairo_arc(cr, x + r, bot - r, r, 0, M_PI);
                cairo_close_path(cr);
            }
            cairo_fill(cr);
        }
        cairo_pattern_destroy(grad);
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);

        if (!text_.empty()) {
            cairo_set_source_rgba(cr, 0.85, 0.87, 0.92, 0.95);

            if (font_face_) {
                cairo_set_font_face(cr, font_face_);
                cairo_set_font_size(cr, 13.0);
            } else {
                cairo_select_font_face(cr, "sans-serif",
                    CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, 13.0);
            }

            double text_pad = pill_r + 4.0;
            double max_w = pw - 2 * text_pad;
            double line_height = 16.0;
            double y = TEXT_Y + 13.0;
            double max_y = ph - 4.0;

            cairo_save(cr);
            cairo_rectangle(cr, text_pad, TEXT_Y - 2, max_w, ph - TEXT_Y + 2);
            cairo_clip(cr);

            std::string line;
            size_t pos = 0;
            while (pos <= text_.size() && y <= max_y) {
                size_t space = text_.find(' ', pos);
                if (space == std::string::npos) space = text_.size();
                std::string word = text_.substr(pos, space - pos);
                std::string candidate = line.empty() ? word : line + " " + word;

                cairo_text_extents_t ext;
                cairo_text_extents(cr, candidate.c_str(), &ext);

                if (ext.width > max_w && !line.empty()) {
                    cairo_move_to(cr, text_pad, y);
                    cairo_show_text(cr, line.c_str());
                    y += line_height;
                    line = word;
                } else {
                    line = candidate;
                }
                pos = space + 1;
            }
            if (!line.empty() && y <= max_y) {
                cairo_move_to(cr, text_pad, y);
                cairo_show_text(cr, line.c_str());
            }

            cairo_restore(cr);
        }
    }

    cairo_restore(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
}

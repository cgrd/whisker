// SPDX-License-Identifier: MIT
#include "overlay_backend.h"

#include <wayland-client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Must come after standard headers: the generated C header uses "namespace" as a param name
#define namespace namespace_
extern "C" {
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
}
#undef namespace

#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static constexpr int MARGIN = 32;

struct BufferData {
    unsigned char* pixels;
    int size;
};

static void bufferRelease(void* data, wl_buffer* buffer) {
    auto* bd = static_cast<BufferData*>(data);
    munmap(bd->pixels, bd->size);
    wl_buffer_destroy(buffer);
    delete bd;
}

static const wl_buffer_listener buffer_listener = {
    .release = bufferRelease,
};

class LayerShellBackend : public OverlayBackend {
public:
    LayerShellBackend(wl_display* display, const std::string& position)
        : display_(display), position_(position) {
        registry_ = wl_display_get_registry(display_);
        wl_registry_add_listener(registry_, &registry_listener, this);
        wl_display_roundtrip(display_);
        if (output_)
            wl_output_add_listener(output_, &output_listener, this);
        wl_display_roundtrip(display_);
    }

    ~LayerShellBackend() override {
        hide();
        if (output_) wl_output_destroy(output_);
        if (layer_shell_) zwlr_layer_shell_v1_destroy(layer_shell_);
        if (shm_) wl_shm_destroy(shm_);
        if (compositor_) wl_compositor_destroy(compositor_);
        wl_registry_destroy(registry_);
        wl_display_disconnect(display_);
    }

    bool usable() const { return compositor_ && shm_ && layer_shell_ && ok(); }

    const char* name() const override { return "layer-shell"; }
    bool ok() const override { return !failed_ && wl_display_get_error(display_) == 0; }
    int outputWidth() const override { return output_width_; }

    void show(int pill_w, int pill_h) override {
        if (surface_ || !ok())
            return;
        width_ = req_w_ = pill_w;
        height_ = req_h_ = pill_h;
        configured_ = false;
        closed_ = false;

        surface_ = wl_compositor_create_surface(compositor_);
        layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "whisker");
        zwlr_layer_surface_v1_set_size(layer_surface_, width_, height_);

        uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
        int top = 0, right = 0, bottom = MARGIN, left = 0;
        if (position_ == "top") {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            top = MARGIN; bottom = 0;
        } else if (position_ == "bottom-left") {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            left = MARGIN;
        } else if (position_ == "bottom-right") {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            right = MARGIN;
        } else if (position_ == "top-left") {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            top = MARGIN; left = MARGIN; bottom = 0;
        } else if (position_ == "top-right") {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            top = MARGIN; right = MARGIN; bottom = 0;
        }

        zwlr_layer_surface_v1_set_anchor(layer_surface_, anchor);
        zwlr_layer_surface_v1_set_margin(layer_surface_, top, right, bottom, left);
        zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, 0);
        zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_surface_listener, this);
        wl_surface_commit(surface_);
        wl_display_roundtrip(display_);
        fprintf(stderr, "overlay: created layer-shell surface (%dx%d)\n", width_, height_);
    }

    void hide() override {
        if (layer_surface_) { zwlr_layer_surface_v1_destroy(layer_surface_); layer_surface_ = nullptr; }
        if (surface_) { wl_surface_destroy(surface_); surface_ = nullptr; }
        wl_display_flush(display_);
    }

    void render(int pill_w, int pill_h, const OverlayPainter& paint) override {
        if (!surface_ || closed_)
            return;
        if (pill_w != req_w_ || pill_h != req_h_) {
            req_w_ = pill_w;
            req_h_ = pill_h;
            zwlr_layer_surface_v1_set_size(layer_surface_, req_w_, req_h_);
            wl_surface_commit(surface_);
            configured_ = false;
        }
        if (!configured_)
            return;

        int w = width_ * scale_;
        int h = height_ * scale_;
        int stride = w * 4;
        int size = stride * h;

        int fd = static_cast<int>(syscall(SYS_memfd_create, "whisker", 0));
        if (fd < 0)
            return;
        if (ftruncate(fd, size) < 0) {
            close(fd);
            return;
        }
        auto* data = static_cast<unsigned char*>(
            mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (data == MAP_FAILED) {
            close(fd);
            return;
        }

        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, size);
        wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);

        paint(data, w, h, stride, scale_, {0, 0, width_, height_});

        wl_buffer_add_listener(buffer, &buffer_listener, new BufferData{data, size});
        wl_surface_attach(surface_, buffer, 0, 0);
        if (scale_ > 1)
            wl_surface_set_buffer_scale(surface_, scale_);
        wl_surface_damage_buffer(surface_, 0, 0, w, h);
        wl_surface_commit(surface_);
        wl_display_flush(display_);
    }

    void dispatch(int timeout_ms) override {
        if (!ok())
            return;
        wl_display_flush(display_);
        pollfd pfd = {wl_display_get_fd(display_), POLLIN, 0};
        int ret = poll(&pfd, 1, timeout_ms) > 0 ? wl_display_dispatch(display_)
                                                : wl_display_dispatch_pending(display_);
        if (ret < 0)
            failed_ = true;
    }

private:
    wl_display* display_;
    std::string position_;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_output* output_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    int32_t width_ = 0, height_ = 0;
    int32_t req_w_ = 0, req_h_ = 0;
    int32_t scale_ = 1;
    int32_t output_width_ = 0;
    bool configured_ = false;
    bool closed_ = false;
    bool failed_ = false;

    static void global(void* data, wl_registry* registry, uint32_t id,
                       const char* interface, uint32_t) {
        auto* self = static_cast<LayerShellBackend*>(data);
        if (strcmp(interface, wl_compositor_interface.name) == 0)
            self->compositor_ = static_cast<wl_compositor*>(
                wl_registry_bind(registry, id, &wl_compositor_interface, 4));
        else if (strcmp(interface, wl_shm_interface.name) == 0)
            self->shm_ = static_cast<wl_shm*>(
                wl_registry_bind(registry, id, &wl_shm_interface, 1));
        else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
            self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
                wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1));
        else if (strcmp(interface, wl_output_interface.name) == 0 && !self->output_)
            self->output_ = static_cast<wl_output*>(
                wl_registry_bind(registry, id, &wl_output_interface, 3));
    }

    static void configure(void* data, zwlr_layer_surface_v1* surface,
                          uint32_t serial, uint32_t w, uint32_t h) {
        auto* self = static_cast<LayerShellBackend*>(data);
        if (w > 0) self->width_ = static_cast<int32_t>(w);
        if (h > 0) self->height_ = static_cast<int32_t>(h);
        self->configured_ = true;
        zwlr_layer_surface_v1_ack_configure(surface, serial);
    }

    static void closed(void* data, zwlr_layer_surface_v1*) {
        static_cast<LayerShellBackend*>(data)->closed_ = true;
    }

    static void outputMode(void* data, wl_output*, uint32_t flags, int32_t w, int32_t, int32_t) {
        if (flags & WL_OUTPUT_MODE_CURRENT)
            static_cast<LayerShellBackend*>(data)->output_width_ = w;
    }

    static void outputScale(void* data, wl_output*, int32_t factor) {
        static_cast<LayerShellBackend*>(data)->scale_ = factor;
    }

    static constexpr wl_registry_listener registry_listener = {
        .global = global,
        .global_remove = [](void*, wl_registry*, uint32_t) {},
    };

    static constexpr zwlr_layer_surface_v1_listener layer_surface_listener = {
        .configure = configure,
        .closed = closed,
    };

    static constexpr wl_output_listener output_listener = {
        .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                       int32_t, const char*, const char*, int32_t) {},
        .mode = outputMode,
        .done = [](void*, wl_output*) {},
        .scale = outputScale,
    };
};

std::vector<std::string> waylandDisplayNames() {
    std::vector<std::string> names;
    const char* env = getenv("WAYLAND_DISPLAY");
    if (env && *env)
        names.push_back(env);
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime)
        return names;
    std::vector<std::string> found;
    for (const auto& entry : std::filesystem::directory_iterator(runtime)) {
        std::string name = entry.path().filename();
        if (name.rfind("wayland-", 0) == 0 && entry.is_socket() &&
            std::find(names.begin(), names.end(), name) == names.end())
            found.push_back(name);
    }
    std::sort(found.begin(), found.end());
    names.insert(names.end(), found.begin(), found.end());
    return names;
}

std::unique_ptr<OverlayBackend> createLayerShellBackend(const std::string& position) {
    for (const std::string& name : waylandDisplayNames()) {
        wl_display* display = wl_display_connect(name.c_str());
        if (!display)
            continue;
        auto backend = std::make_unique<LayerShellBackend>(display, position);
        if (backend->usable()) {
            fprintf(stderr, "overlay: connected to wayland display '%s'\n", name.c_str());
            return backend;
        }
        fprintf(stderr, "overlay: wayland display '%s' lacks layer-shell\n", name.c_str());
    }
    return nullptr;
}

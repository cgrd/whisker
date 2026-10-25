// SPDX-License-Identifier: MIT
#include "overlay_backend.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

static constexpr int MARGIN = 32;

static std::atomic<bool> g_x_io_error{false};

static int onXError(Display*, XErrorEvent* ev) {
    fprintf(stderr, "overlay: X error code %d (request %d)\n", ev->error_code, ev->request_code);
    return 0;
}

static int onXIOError(Display*) {
    g_x_io_error.store(true);
    return 0;
}

static void onXIOErrorExit(Display*, void*) {
    g_x_io_error.store(true);
}

static bool socketConnectable(const std::string& path) {
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path))
        return false;
    addr.sun_family = AF_UNIX;
    path.copy(addr.sun_path, path.size());
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    bool ok = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd);
    return ok;
}

bool gamescopeRunning() {
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime)
        return false;
    const char* name = getenv("GAMESCOPE_WAYLAND_DISPLAY");
    std::string dir = std::string(runtime) + "/";
    return socketConnectable(dir + "gamescope-0") ||
           (name && socketConnectable(dir + name));
}

class GamescopeBackend : public OverlayBackend {
public:
    GamescopeBackend(Display* display, const std::string& position)
        : display_(display), position_(position) {
        screen_ = DefaultScreen(display_);
        width_ = DisplayWidth(display_, screen_);
        height_ = DisplayHeight(display_, screen_);
        overlay_atom_ = XInternAtom(display_, "STEAM_OVERLAY", False);
        no_focus_atom_ = XInternAtom(display_, "GAMESCOPE_NO_FOCUS", False);
        pixels_.resize(static_cast<size_t>(width_) * height_ * 4);
    }

    ~GamescopeBackend() override {
        hide();
        XCloseDisplay(display_);
    }

    const char* name() const override { return "gamescope"; }
    bool ok() const override { return !g_x_io_error.load(); }
    int outputWidth() const override { return width_; }

    void show(int, int) override {
        if (window_ || !ok())
            return;
        XVisualInfo vinfo;
        if (!XMatchVisualInfo(display_, screen_, 32, TrueColor, &vinfo)) {
            fprintf(stderr, "overlay: no 32-bit TrueColor visual\n");
            return;
        }
        visual_ = vinfo.visual;

        Window root = RootWindow(display_, screen_);
        XSetWindowAttributes attr = {};
        colormap_ = XCreateColormap(display_, root, vinfo.visual, AllocNone);
        attr.colormap = colormap_;
        attr.override_redirect = True;
        window_ = XCreateWindow(display_, root, 0, 0, width_, height_, 0,
            vinfo.depth, InputOutput, vinfo.visual,
            CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect, &attr);
        gc_ = XCreateGC(display_, window_, 0, nullptr);

        setCardinal(overlay_atom_, 1);
        setCardinal(no_focus_atom_, 1);
        XMapWindow(display_, window_);
        XFlush(display_);
        fprintf(stderr, "overlay: created gamescope X11 window %lu (%dx%d)\n",
                window_, width_, height_);
    }

    void hide() override {
        if (!window_)
            return;
        setCardinal(overlay_atom_, 0);
        XFreeGC(display_, gc_);
        XDestroyWindow(display_, window_);
        XFreeColormap(display_, colormap_);
        XFlush(display_);
        gc_ = nullptr;
        window_ = 0;
        colormap_ = 0;
    }

    void render(int pill_w, int pill_h, const OverlayPainter& paint) override {
        if (!window_)
            return;
        int stride = width_ * 4;
        paint(pixels_.data(), width_, height_, stride, 1, pillRect(pill_w, pill_h));

        XImage* img = XCreateImage(display_, visual_, 32, ZPixmap, 0,
            reinterpret_cast<char*>(pixels_.data()), width_, height_, 32, stride);
        XPutImage(display_, window_, gc_, img, 0, 0, 0, 0, width_, height_);
        img->data = nullptr;
        XDestroyImage(img);
        XFlush(display_);
    }

    void dispatch(int timeout_ms) override {
        while (ok() && XPending(display_) > 0) {
            XEvent ev;
            XNextEvent(display_, &ev);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    }

private:
    Display* display_;
    std::string position_;
    int screen_ = 0;
    int width_ = 0;
    int height_ = 0;
    Atom overlay_atom_ = 0;
    Atom no_focus_atom_ = 0;
    Window window_ = 0;
    Colormap colormap_ = 0;
    GC gc_ = nullptr;
    Visual* visual_ = nullptr;
    std::vector<unsigned char> pixels_;

    void setCardinal(Atom atom, uint32_t value) {
        unsigned long v = value;
        XChangeProperty(display_, window_, atom, XA_CARDINAL, 32, PropModeReplace,
            reinterpret_cast<unsigned char*>(&v), 1);
    }

    PillRect pillRect(int pw, int ph) const {
        double left = MARGIN, right = width_ - pw - MARGIN;
        double top = MARGIN, bottom = height_ - ph - MARGIN;
        double center = (width_ - pw) / 2.0;
        if (position_ == "top") return {center, top, pw, ph};
        if (position_ == "top-left") return {left, top, pw, ph};
        if (position_ == "top-right") return {right, top, pw, ph};
        if (position_ == "bottom-left") return {left, bottom, pw, ph};
        if (position_ == "bottom-right") return {right, bottom, pw, ph};
        return {center, bottom, pw, ph};
    }
};

std::unique_ptr<OverlayBackend> createGamescopeBackend(const std::string& position) {
    static bool handlers_installed = false;
    if (!handlers_installed) {
        XSetErrorHandler(onXError);
        XSetIOErrorHandler(onXIOError);
        handlers_installed = true;
    }

    const char* env = getenv("DISPLAY");
    std::string name = env && *env ? env : ":0";
    Display* display = XOpenDisplay(name.c_str());
    if (!display) {
        fprintf(stderr, "overlay: cannot open X display '%s'\n", name.c_str());
        return nullptr;
    }
    XSetIOErrorExitHandler(display, onXIOErrorExit, nullptr);
    g_x_io_error.store(false);
    fprintf(stderr, "overlay: connected to X display '%s'\n", name.c_str());
    return std::make_unique<GamescopeBackend>(display, position);
}

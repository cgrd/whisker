// SPDX-License-Identifier: MIT
#include "check.h"
#include "overlay_backend.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string g_dir;

static int bindSocket(const std::string& name, bool listening) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = g_dir + "/" + name;
    path.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (listening) {
        listen(fd, 1);
        return fd;
    }
    close(fd);
    return -1;
}

static void resetDir() {
    fs::remove_all(g_dir);
    fs::create_directory(g_dir);
    unsetenv("GAMESCOPE_WAYLAND_DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    setenv("XDG_RUNTIME_DIR", g_dir.c_str(), 1);
}

static void testNoGamescope() {
    resetDir();
    CHECK(!gamescopeRunning());
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(!gamescopeRunning());
}

static void testStaleGamescopeSocketIgnored() {
    resetDir();
    bindSocket("gamescope-0", false);
    CHECK(fs::exists(g_dir + "/gamescope-0"));
    CHECK(!gamescopeRunning());
}

static void testLiveGamescopeSocket() {
    resetDir();
    int fd = bindSocket("gamescope-0", true);
    CHECK(gamescopeRunning());
    close(fd);
}

static void testGamescopeDisplayFromEnv() {
    resetDir();
    int fd = bindSocket("gamescope-1", true);
    CHECK(!gamescopeRunning());
    setenv("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-1", 1);
    CHECK(gamescopeRunning());
    close(fd);
}

static void testWaylandDisplayOrder() {
    resetDir();
    CHECK(waylandDisplayNames().empty());

    int a = bindSocket("wayland-1", true);
    int b = bindSocket("wayland-0", true);
    std::ofstream(g_dir + "/wayland-0.lock") << "";
    std::ofstream(g_dir + "/wayland-2") << "";
    CHECK(waylandDisplayNames() == std::vector<std::string>{"wayland-0", "wayland-1"});

    setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    CHECK(waylandDisplayNames() == std::vector<std::string>{"wayland-1", "wayland-0"});

    setenv("WAYLAND_DISPLAY", "wayland-9", 1);
    CHECK(waylandDisplayNames() == std::vector<std::string>{"wayland-9", "wayland-0", "wayland-1"});
    close(a);
    close(b);
}

int main() {
    std::string tmpl = (fs::temp_directory_path() / "whisker-overlay-XXXXXX").string();
    g_dir = mkdtemp(tmpl.data());
    testNoGamescope();
    testStaleGamescopeSocketIgnored();
    testLiveGamescopeSocket();
    testGamescopeDisplayFromEnv();
    testWaylandDisplayOrder();
    fs::remove_all(g_dir);
    return checkReport();
}

// SPDX-License-Identifier: MIT
#include "check.h"
#include "command.h"

#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string socketPathWith(const char* runtime) {
    if (runtime)
        setenv("XDG_RUNTIME_DIR", runtime, 1);
    else
        unsetenv("XDG_RUNTIME_DIR");
    return commandSocketPath();
}

static void testRuntimeDir(const fs::path& base) {
    fs::path good = base / "run";
    fs::create_directory(good);
    chmod(good.c_str(), 0700);
    CHECK_EQ(socketPathWith(good.c_str()), (good / "whisker.sock").string());

    // Without a safe runtime dir there is no socket path, never a /tmp fallback
    CHECK_EQ(socketPathWith(nullptr), std::string());
    CHECK_EQ(socketPathWith(""), std::string());
    CHECK_EQ(socketPathWith("relative/run"), std::string());
    CHECK_EQ(socketPathWith((base / "missing").c_str()), std::string());

    fs::path shared = base / "shared";
    fs::create_directory(shared);
    chmod(shared.c_str(), 01777);
    CHECK_EQ(socketPathWith(shared.c_str()), std::string());
    chmod(shared.c_str(), 0750);
    CHECK_EQ(socketPathWith(shared.c_str()), std::string());

    fs::path link = base / "link";
    fs::create_directory_symlink(good, link);
    CHECK_EQ(socketPathWith(link.c_str()), std::string());

    std::string error;
    setenv("XDG_RUNTIME_DIR", shared.c_str(), 1);
    CHECK_EQ(runtimeDir(error), std::string());
    CHECK(error.find("0700") != std::string::npos);
}

static void testInitRefusesEmptyPath() {
    CommandSocket socket;
    CHECK(!socket.init(""));
    std::string reply;
    CHECK(!sendCommand("", "status", reply, 100));
}

int main() {
    fs::path base = fs::temp_directory_path() / ("whisker-test-command-" + std::to_string(getpid()));
    fs::create_directories(base);
    testRuntimeDir(base);
    testInitRefusesEmptyPath();
    fs::remove_all(base);
    return checkReport();
}

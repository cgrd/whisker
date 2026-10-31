// SPDX-License-Identifier: MIT
#include "check.h"
#include "spawn.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

using Args = std::vector<std::string>;

static void testSplit() {
    CHECK(splitCommand("notify-send hello") == Args{"notify-send", "hello"});
    CHECK(splitCommand("  a   b\tc ") == Args{"a", "b", "c"});
    CHECK(splitCommand("notify-send \"hello world\"") == Args{"notify-send", "hello world"});
    CHECK(splitCommand("echo 'it''s' \"a \\\"q\\\"\"") == Args{"echo", "its", "a \"q\""});
    CHECK(splitCommand("a\\ b c") == Args{"a b", "c"});
    CHECK(splitCommand("x \"\"") == Args{"x", ""});
    CHECK(splitCommand("echo $HOME | wc; rm") == Args{"echo", "$HOME", "|", "wc;", "rm"});
    CHECK(splitCommand("").empty());
}

static void testSpawnRunsWithoutShell() {
    namespace fs = std::filesystem;
    fs::path marker = fs::temp_directory_path() / "whisker-spawn-test";
    fs::remove(marker);
    CHECK(spawnDetached("touch '" + marker.string() + "'"));
    for (int i = 0; i < 50 && !fs::exists(marker); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(fs::exists(marker));
    fs::remove(marker);

    fs::path injected = fs::temp_directory_path() / "whisker-spawn-injected";
    fs::remove(injected);
    spawnDetached("true; touch " + injected.string());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(!fs::exists(injected));
    CHECK(!spawnDetached(""));
}

static void testRunAndWait() {
    CHECK_EQ(runAndWait({"true"}), 0);
    CHECK_EQ(runAndWait({"sh", "-c", "exit 3"}), 3);
    CHECK_EQ(runAndWait({"whisker-no-such-command"}), 127);
    CHECK_EQ(runAndWait({}), -1);
}

int main() {
    testSplit();
    testSpawnRunsWithoutShell();
    testRunAndWait();
    return checkReport();
}

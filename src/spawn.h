// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

// Splits a command line into arguments. Single and double quotes group words and
// backslash escapes the next character; there is no variable, glob or pipe handling.
std::vector<std::string> splitCommand(const std::string& command);

// Runs the command without a shell and without waiting for it. Returns false if it
// couldn't be started.
bool spawnDetached(const std::string& command);

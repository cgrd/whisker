// SPDX-License-Identifier: MIT
#pragma once
#include <string>

// Check: report whether a newer verified release exists. Install: whisker --update. Scheduled:
// the daily timer, which does nothing while auto_update is off.
enum class UpdateMode { Check, Install, Scheduled };

// Returns the process exit code.
int runUpdate(UpdateMode mode, const std::string& config_path);

// whisker --verify-release DIR: checks release assets downloaded by install.sh against the
// signed manifest in DIR with the keys built into this binary. Returns the process exit code.
int runVerifyRelease(const std::string& dir);

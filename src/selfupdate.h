// SPDX-License-Identifier: MIT
#pragma once
#include <string>

// Check: report whether a newer verified release exists. Install: whisker --update. Scheduled:
// the daily timer, which does nothing while auto_update is off.
enum class UpdateMode { Check, Install, Scheduled };

// Returns the process exit code.
int runUpdate(UpdateMode mode, const std::string& config_path);

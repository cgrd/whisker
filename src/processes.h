// SPDX-License-Identifier: MIT
#pragma once
#include "chat_profile.h"
#include <vector>

// The current user's processes; ones that vanish or can't be read are skipped.
std::vector<ProcessInfo> readUserProcesses();

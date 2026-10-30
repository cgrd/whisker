// SPDX-License-Identifier: MIT
#pragma once
#include "chat_profile.h"
#include <string>

std::string processDictation(const std::string& text);
std::string autoCapitalize(const std::string& text);
// Runs the chat profile, if any, before the other steps so they leave the channel shortcut alone.
std::string formatTranscript(const std::string& text, const ChatProfile* chat, bool commands, bool capitalize);

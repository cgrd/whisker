// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

struct ChatChannel {
    std::string name;
    std::string command;
    std::vector<std::string> say;
    bool target = false;
};

struct ChatProfile {
    std::string open_key;
    std::vector<ChatChannel> channels;
    std::vector<std::pair<std::string, std::string>> names;
};

using ChatProfiles = std::map<std::string, ChatProfile>;

// A dictated chat line split into the channel shortcut and the message.
struct ChatLine {
    std::string prefix;
    std::string body;
};

std::vector<std::string> chatProfileNames(const ChatProfiles& custom);
// Built-in profiles are merged with a custom profile of the same name.
bool resolveChatProfile(const std::string& name, const ChatProfiles& custom, ChatProfile& out);
ChatLine splitChatLine(const ChatProfile& profile, const std::string& text);

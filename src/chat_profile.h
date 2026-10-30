// SPDX-License-Identifier: MIT
#pragma once
#include <initializer_list>
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
    std::vector<std::string> match;
};

// Keeps profiles in config order, which decides which one auto selection picks first.
class ChatProfiles {
public:
    using Entry = std::pair<std::string, ChatProfile>;
    ChatProfiles() = default;
    ChatProfiles(std::initializer_list<Entry> entries) : entries_(entries) {}
    ChatProfile& operator[](const std::string& name);
    const ChatProfile* find(const std::string& name) const;
    std::vector<Entry>::const_iterator begin() const { return entries_.begin(); }
    std::vector<Entry>::const_iterator end() const { return entries_.end(); }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
};

struct ProcessInfo {
    std::vector<std::string> argv;
    std::string comm;
};

inline const char* kChatProfileAuto = "auto";
inline const char* kChatProfileNone = "none";

// A dictated chat line split into the channel shortcut and the message.
struct ChatLine {
    std::string prefix;
    std::string body;
};

std::vector<std::string> chatProfileNames(const ChatProfiles& custom);
// Built-in profiles are merged with a custom profile of the same name.
bool resolveChatProfile(const std::string& name, const ChatProfiles& custom, ChatProfile& out);
bool chatProfileOff(const std::string& setting);
bool chatProfileReserved(const std::string& name);
bool processMatches(const ProcessInfo& proc, const std::string& pattern);
// Custom profiles in config order, then the remaining built-in ones; empty when none matches.
std::string autoChatProfile(const ChatProfiles& custom, const std::vector<ProcessInfo>& procs);
ChatLine splitChatLine(const ChatProfile& profile, const std::string& text);

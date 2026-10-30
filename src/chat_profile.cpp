// SPDX-License-Identifier: MIT
#include "chat_profile.h"
#include <algorithm>
#include <cctype>

static const ChatProfiles& builtinProfiles() {
    static const ChatProfiles m = {
        {"wow", {"enter", {
            {"say", "/s", {"say"}},
            {"yell", "/y", {"yell", "shout"}},
            {"emote", "/e", {"emote"}},
            {"party", "/p", {"party", "party chat", "to party", "tell party"}},
            {"instance", "/i", {"instance", "instance chat", "to instance", "tell instance"}},
            {"raid", "/ra", {"raid", "raid chat", "to raid", "tell raid"}},
            {"raid_warning", "/rw", {"raid warning"}},
            {"guild", "/g", {"guild", "guild chat", "to guild", "tell guild"}},
            {"officer", "/o", {"officer", "officer chat", "officers"}},
            {"whisper", "/w", {"whisper", "tell", "message"}, true},
            {"reply", "/r", {"reply"}},
            {"general", "/1", {"general", "general chat"}},
            {"trade", "/2", {"trade", "trade chat"}},
        }, {}}},
    };
    return m;
}

std::vector<std::string> chatProfileNames(const ChatProfiles& custom) {
    std::vector<std::string> names;
    for (const auto& [name, _] : builtinProfiles())
        names.push_back(name);
    for (const auto& [name, _] : custom)
        if (!builtinProfiles().count(name))
            names.push_back(name);
    return names;
}

static void mergeProfile(ChatProfile& base, const ChatProfile& extra) {
    if (!extra.open_key.empty())
        base.open_key = extra.open_key;
    for (const auto& ch : extra.channels) {
        auto it = std::find_if(base.channels.begin(), base.channels.end(),
                               [&](const ChatChannel& c) { return c.name == ch.name; });
        if (it != base.channels.end())
            *it = ch;
        else
            base.channels.push_back(ch);
    }
    base.names.insert(base.names.end(), extra.names.begin(), extra.names.end());
}

bool resolveChatProfile(const std::string& name, const ChatProfiles& custom, ChatProfile& out) {
    if (name.empty())
        return false;
    auto builtin = builtinProfiles().find(name);
    auto user = custom.find(name);
    if (builtin == builtinProfiles().end() && user == custom.end())
        return false;
    out = builtin != builtinProfiles().end() ? builtin->second : ChatProfile{};
    if (user != custom.end())
        mergeProfile(out, user->second);
    return true;
}

static bool isAlnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

static std::string normalize(const std::string& word) {
    std::string out;
    for (char c : word)
        if (isAlnum(c))
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static std::string stripEdgePunctuation(const std::string& word) {
    size_t start = 0, end = word.size();
    while (start < end && !isAlnum(word[start])) ++start;
    while (end > start && !isAlnum(word[end - 1])) --end;
    return word.substr(start, end - start);
}

struct Token {
    size_t start;
    std::string raw;
    std::string norm;
};

static std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > start)
            tokens.push_back({start, text.substr(start, i - start), normalize(text.substr(start, i - start))});
    }
    return tokens;
}

static std::vector<std::string> normalizedWords(const std::string& phrase) {
    std::vector<std::string> words;
    for (const auto& t : tokenize(phrase))
        if (!t.norm.empty())
            words.push_back(t.norm);
    return words;
}

static bool wordsMatch(const std::vector<Token>& tokens, size_t at, const std::vector<std::string>& words) {
    if (words.empty() || at + words.size() > tokens.size())
        return false;
    for (size_t i = 0; i < words.size(); ++i)
        if (tokens[at + i].norm != words[i])
            return false;
    return true;
}

static std::string digitWord(const std::string& word) {
    static const char* numbers[] = {"zero", "one", "two", "three", "four", "five",
                                    "six", "seven", "eight", "nine", "ten"};
    for (int i = 0; i <= 10; ++i)
        if (word == numbers[i])
            return std::to_string(i);
    return word;
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static const ChatChannel* channelForCommand(const ChatProfile& profile, const std::string& command) {
    for (const auto& ch : profile.channels)
        if (toLower(ch.command) == command)
            return &ch;
    return nullptr;
}

// Returns the number of tokens the target used, 0 when there is none.
static size_t matchTarget(const ChatProfile& profile, const std::vector<Token>& tokens, size_t at,
                          std::string& target) {
    size_t best = 0;
    for (const auto& [spoken, name] : profile.names) {
        auto words = normalizedWords(spoken);
        if (words.size() > best && wordsMatch(tokens, at, words)) {
            best = words.size();
            target = name;
        }
    }
    if (best > 0 || at >= tokens.size())
        return best;
    target = stripEdgePunctuation(tokens[at].raw);
    return target.empty() ? 0 : 1;
}

ChatLine splitChatLine(const ChatProfile& profile, const std::string& text) {
    ChatLine none{"", text};
    auto tokens = tokenize(text);
    if (tokens.empty())
        return none;

    std::string command;
    const ChatChannel* channel = nullptr;
    size_t used = 0;
    if (tokens[0].raw[0] == '/' && !tokens[0].norm.empty()) {
        command = "/" + tokens[0].norm;
        used = 1;
    } else if (tokens[0].norm == "slash" && tokens.size() > 1 && !tokens[1].norm.empty()) {
        command = "/" + digitWord(tokens[1].norm);
        used = 2;
    } else {
        for (const auto& ch : profile.channels) {
            for (const auto& alias : ch.say) {
                auto words = normalizedWords(alias);
                if (words.size() > used && wordsMatch(tokens, 0, words)) {
                    used = words.size();
                    channel = &ch;
                }
            }
        }
        if (!channel)
            return none;
        command = channel->command;
    }
    if (!channel)
        channel = channelForCommand(profile, command);

    std::string prefix = command + " ";
    if (channel && channel->target) {
        std::string target;
        size_t n = matchTarget(profile, tokens, used, target);
        if (n == 0)
            return none;
        prefix += target + " ";
        used += n;
    }
    while (used < tokens.size() && tokens[used].norm.empty())
        ++used;
    return {prefix, used < tokens.size() ? text.substr(tokens[used].start) : ""};
}

// SPDX-License-Identifier: MIT
#include "chat_profile.h"
#include <algorithm>
#include <cctype>

ChatProfile& ChatProfiles::operator[](const std::string& name) {
    for (auto& [n, profile] : entries_)
        if (n == name)
            return profile;
    entries_.emplace_back(name, ChatProfile{});
    return entries_.back().second;
}

const ChatProfile* ChatProfiles::find(const std::string& name) const {
    for (const auto& [n, profile] : entries_)
        if (n == name)
            return &profile;
    return nullptr;
}

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
        }, {}, {"Wow.exe", "WowClassic.exe", "WowB.exe", "WowT.exe", "WowClassicB.exe", "WowClassicT.exe"}}},
    };
    return m;
}

std::vector<std::string> chatProfileNames(const ChatProfiles& custom) {
    std::vector<std::string> names;
    for (const auto& [name, _] : builtinProfiles())
        names.push_back(name);
    for (const auto& [name, _] : custom)
        if (!builtinProfiles().find(name))
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
    base.match.insert(base.match.end(), extra.match.begin(), extra.match.end());
}

bool resolveChatProfile(const std::string& name, const ChatProfiles& custom, ChatProfile& out) {
    if (chatProfileReserved(name))
        return false;
    const ChatProfile* builtin = builtinProfiles().find(name);
    const ChatProfile* user = custom.find(name);
    if (!builtin && !user)
        return false;
    out = builtin ? *builtin : ChatProfile{};
    if (user)
        mergeProfile(out, *user);
    return true;
}

bool chatProfileOff(const std::string& setting) {
    return setting.empty() || setting == kChatProfileNone;
}

bool chatProfileReserved(const std::string& name) {
    return chatProfileOff(name) || name == kChatProfileAuto;
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

static std::string pathKey(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return toLower(s);
}

static std::string exeName(const ProcessInfo& proc) {
    if (!proc.argv.empty()) {
        const std::string& exe = proc.argv[0];
        size_t slash = exe.find_last_of("/\\");
        std::string base = slash == std::string::npos ? exe : exe.substr(slash + 1);
        if (!base.empty())
            return base;
    }
    return proc.comm;
}

bool processMatches(const ProcessInfo& proc, const std::string& pattern) {
    if (pattern.empty())
        return false;
    if (pattern.find_first_of("/\\") == std::string::npos)
        return toLower(exeName(proc)) == toLower(pattern);
    std::string cmdline;
    for (const auto& arg : proc.argv)
        cmdline += (cmdline.empty() ? "" : " ") + arg;
    return pathKey(cmdline).find(pathKey(pattern)) != std::string::npos;
}

std::string autoChatProfile(const ChatProfiles& custom, const std::vector<ProcessInfo>& procs) {
    std::vector<std::string> order;
    for (const auto& [name, _] : custom)
        order.push_back(name);
    for (const auto& [name, _] : builtinProfiles())
        if (!custom.find(name))
            order.push_back(name);
    for (const auto& name : order) {
        ChatProfile profile;
        if (!resolveChatProfile(name, custom, profile))
            continue;
        for (const auto& pattern : profile.match)
            for (const auto& proc : procs)
                if (processMatches(proc, pattern))
                    return name;
    }
    return "";
}

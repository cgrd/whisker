// SPDX-License-Identifier: MIT
#include "dictation.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

enum class Spacing {
    Word,
    AttachLeft,
    AttachRight,
    Join,
    Break,
};

struct Replacement {
    std::string text;
    Spacing spacing;
};

static const std::unordered_map<std::string, Replacement>& commandMap() {
    static const std::unordered_map<std::string, Replacement> m = {
        {"period", {".", Spacing::AttachLeft}},
        {"full stop", {".", Spacing::AttachLeft}},
        {"comma", {",", Spacing::AttachLeft}},
        {"question mark", {"?", Spacing::AttachLeft}},
        {"exclamation mark", {"!", Spacing::AttachLeft}},
        {"exclamation point", {"!", Spacing::AttachLeft}},
        {"colon", {":", Spacing::AttachLeft}},
        {"semicolon", {";", Spacing::AttachLeft}},
        {"new line", {"\n", Spacing::Break}},
        {"newline", {"\n", Spacing::Break}},
        {"new paragraph", {"\n\n", Spacing::Break}},
        {"open quote", {"\"", Spacing::AttachRight}},
        {"close quote", {"\"", Spacing::AttachLeft}},
        {"open paren", {"(", Spacing::AttachRight}},
        {"close paren", {")", Spacing::AttachLeft}},
        {"hyphen", {"-", Spacing::Join}},
        {"dash", {"-", Spacing::Word}},
        {"ellipsis", {"...", Spacing::AttachLeft}},
    };
    return m;
}

static std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string processDictation(const std::string& text) {
    const auto& cmds = commandMap();

    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string w;
    while (iss >> w) words.push_back(w);

    std::string result;
    bool need_space = false;

    for (size_t i = 0; i < words.size(); ) {
        Replacement plain{words[i], Spacing::Word};
        const Replacement* repl = &plain;
        size_t consumed = 1;

        if (i + 1 < words.size()) {
            auto it = cmds.find(toLower(words[i]) + " " + toLower(words[i + 1]));
            if (it != cmds.end()) { repl = &it->second; consumed = 2; }
        }
        if (consumed == 1) {
            auto it = cmds.find(toLower(words[i]));
            if (it != cmds.end()) repl = &it->second;
        }

        bool space_before = need_space &&
            (repl->spacing == Spacing::Word || repl->spacing == Spacing::AttachRight);
        if (space_before) result += ' ';
        result += repl->text;
        need_space = repl->spacing == Spacing::Word || repl->spacing == Spacing::AttachLeft;
        i += consumed;
    }

    return result;
}

std::string autoCapitalize(const std::string& text) {
    if (text.empty()) return text;
    std::string out = text;
    bool cap_next = true;
    for (size_t i = 0; i < out.size(); i++) {
        if (cap_next && std::isalpha(static_cast<unsigned char>(out[i]))) {
            out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
            cap_next = false;
        } else if (out[i] == '.' || out[i] == '?' || out[i] == '!') {
            cap_next = true;
        } else if (out[i] == '\n') {
            cap_next = true;
        }
    }
    return out;
}

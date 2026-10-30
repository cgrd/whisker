// SPDX-License-Identifier: MIT
#include "check.h"
#include "chat_profile.h"

static ChatProfile wow(const ChatProfiles& custom = {}) {
    ChatProfile p;
    CHECK(resolveChatProfile("wow", custom, p));
    return p;
}

static std::string chat(const ChatProfile& p, const std::string& text) {
    ChatLine line = splitChatLine(p, text);
    return line.prefix + line.body;
}

static void testEveryBuiltinChannel() {
    ChatProfile p = wow();
    CHECK_EQ(p.open_key, "enter");
    CHECK_EQ(chat(p, "say hello there"), "/s hello there");
    CHECK_EQ(chat(p, "yell for the horde"), "/y for the horde");
    CHECK_EQ(chat(p, "shout for the horde"), "/y for the horde");
    CHECK_EQ(chat(p, "emote waves"), "/e waves");
    CHECK_EQ(chat(p, "party pull in five"), "/p pull in five");
    CHECK_EQ(chat(p, "instance need a tank"), "/i need a tank");
    CHECK_EQ(chat(p, "raid bloodlust now"), "/ra bloodlust now");
    CHECK_EQ(chat(p, "raid warning stack up"), "/rw stack up");
    CHECK_EQ(chat(p, "guild chat anyone up for a raid"), "/g anyone up for a raid");
    CHECK_EQ(chat(p, "officer promote him"), "/o promote him");
    CHECK_EQ(chat(p, "whisper Thrall need a summon"), "/w Thrall need a summon");
    CHECK_EQ(chat(p, "reply on my way"), "/r on my way");
    CHECK_EQ(chat(p, "general hello zone"), "/1 hello zone");
    CHECK_EQ(chat(p, "trade chat WTS ore"), "/2 WTS ore");
}

static void testCaseAndPunctuation() {
    ChatProfile p = wow();
    CHECK_EQ(chat(p, "Party, pull in five."), "/p pull in five.");
    CHECK_EQ(chat(p, " PARTY: pull"), "/p pull");
    CHECK_EQ(chat(p, "Guild chat. Anyone up?"), "/g Anyone up?");
    CHECK_EQ(chat(p, "Party - pull"), "/p pull");
    CHECK_EQ(chat(p, "Tell party, ready"), "/p ready");
}

static void testLongestAliasWins() {
    ChatProfile p = wow();
    CHECK_EQ(chat(p, "raid warning, stack"), "/rw stack");
    CHECK_EQ(chat(p, "raid, warning stack"), "/rw stack");
    CHECK_EQ(chat(p, "raid chat go"), "/ra go");
    CHECK_EQ(chat(p, "tell guild hi"), "/g hi");
    CHECK_EQ(chat(p, "tell Jaina hi"), "/w Jaina hi");
}

static void testSlash() {
    ChatProfile p = wow();
    CHECK_EQ(chat(p, "slash p pull in five"), "/p pull in five");
    CHECK_EQ(chat(p, "Slash P. Pull in five."), "/p Pull in five.");
    CHECK_EQ(chat(p, "/P. pull"), "/p pull");
    CHECK_EQ(chat(p, "slash one hello"), "/1 hello");
    CHECK_EQ(chat(p, "slash 2 WTS ore"), "/2 WTS ore");
    CHECK_EQ(chat(p, "slash dance"), "/dance ");
    CHECK_EQ(chat(p, "slash w Thrall hi"), "/w Thrall hi");
    CHECK_EQ(chat(p, "slash"), "slash");
}

static void testWhisper() {
    ChatProfile p = wow();
    CHECK_EQ(chat(p, "whisper Thrall, need a summon"), "/w Thrall need a summon");
    CHECK_EQ(chat(p, "whisper"), "whisper");
    CHECK_EQ(chat(p, "Whisper."), "Whisper.");

    ChatProfiles custom;
    custom["wow"].names = {{"thrall", "Thrall"}, {"jaina proudmoore", "Jaina"}, {"bob", "Bobby"}};
    ChatProfile named = wow(custom);
    CHECK_EQ(chat(named, "whisper thrall hi"), "/w Thrall hi");
    CHECK_EQ(chat(named, "Tell Jaina Proudmoore, on my way"), "/w Jaina on my way");
    CHECK_EQ(chat(named, "whisper Bob. hi"), "/w Bobby hi");
    CHECK_EQ(chat(named, "whisper Garrosh hi"), "/w Garrosh hi");
}

static void testNoMatchPassesThrough() {
    ChatProfile p = wow();
    CHECK_EQ(chat(p, "pull in five party"), "pull in five party");
    CHECK_EQ(chat(p, "Partying tonight"), "Partying tonight");
    CHECK_EQ(chat(p, ""), "");
    ChatLine line = splitChatLine(p, "hello there");
    CHECK_EQ(line.prefix, "");
    CHECK_EQ(line.body, "hello there");
    CHECK_EQ(chat(p, "party"), "/p ");
}

static void testResolve() {
    ChatProfile p;
    CHECK(!resolveChatProfile("", {}, p));
    CHECK(!resolveChatProfile("ffxiv", {}, p));

    ChatProfiles custom;
    custom["ffxiv"].open_key = "space";
    custom["ffxiv"].channels = {{"fc", "/fc", {"free company"}, false}};
    CHECK(resolveChatProfile("ffxiv", custom, p));
    CHECK_EQ(p.open_key, "space");
    CHECK_EQ(chat(p, "free company hi"), "/fc hi");
    CHECK_EQ(chat(p, "party hi"), "party hi");

    custom["wow"].open_key = "slash";
    custom["wow"].channels = {{"party", "/party", {"group"}, false}, {"lfg", "/4", {"looking for group"}, false}};
    ChatProfile merged = wow(custom);
    CHECK_EQ(merged.open_key, "slash");
    CHECK_EQ(chat(merged, "group hi"), "/party hi");
    CHECK_EQ(chat(merged, "party hi"), "party hi");
    CHECK_EQ(chat(merged, "looking for group LFM"), "/4 LFM");
    CHECK_EQ(chat(merged, "guild hi"), "/g hi");

    auto names = chatProfileNames(custom);
    CHECK_EQ(names.size(), 2u);
    CHECK_EQ(names[0], "wow");
    CHECK_EQ(names[1], "ffxiv");
}

int main() {
    testEveryBuiltinChannel();
    testCaseAndPunctuation();
    testLongestAliasWins();
    testSlash();
    testWhisper();
    testNoMatchPassesThrough();
    testResolve();
    return checkReport();
}

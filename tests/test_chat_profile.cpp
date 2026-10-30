// SPDX-License-Identifier: MIT
#include "check.h"
#include "chat_profile.h"
#include "processes.h"

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
    CHECK(!resolveChatProfile("auto", {}, p));
    CHECK(!resolveChatProfile("none", {}, p));
    CHECK(chatProfileOff(""));
    CHECK(chatProfileOff("none"));
    CHECK(!chatProfileOff("auto"));
    CHECK(chatProfileReserved("auto"));
    CHECK(!chatProfileReserved("wow"));

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

static ProcessInfo proc(std::vector<std::string> argv, std::string comm = "") {
    return {std::move(argv), std::move(comm)};
}

static void testProcessMatches() {
    ProcessInfo forever = proc({"C:\\Program Files (x86)\\World of Warcraft\\_forever_\\Wow.exe", "-launcherlogin"},
                               "Wow.exe");
    CHECK(processMatches(forever, "Wow.exe"));
    CHECK(processMatches(forever, "wow.EXE"));
    CHECK(!processMatches(forever, "WowClassic.exe"));
    CHECK(processMatches(forever, "_forever_/Wow.exe"));
    CHECK(processMatches(forever, "_FOREVER_\\wow.exe"));
    CHECK(processMatches(forever, "World of Warcraft/_forever_"));
    CHECK(!processMatches(forever, "_retail_/Wow.exe"));
    CHECK(!processMatches(forever, ""));

    ProcessInfo zdrive = proc({"Z:\\home\\me\\Games/World of Warcraft/_retail_/Wow.exe"});
    CHECK(processMatches(zdrive, "Wow.exe"));
    CHECK(processMatches(zdrive, "_retail_/Wow.exe"));

    CHECK(processMatches(proc({}, "WowClassic.exe"), "WowClassic.exe"));
    CHECK(processMatches(proc({"C:\\Games\\"}, "Wow.exe"), "Wow.exe"));
    CHECK(!processMatches(proc({"/usr/bin/wow.exe.sh"}, "Wow.exe"), "Wow.exe"));
    CHECK(!processMatches(proc({"/usr/bin/vim", "Wow.exe"}), "Wow.exe"));
    CHECK(processMatches(proc({"/usr/bin/vim", "/tmp/_forever_/Wow.exe"}), "_forever_/Wow.exe"));
}

static void testAutoChatProfile() {
    std::vector<ProcessInfo> idle = {proc({"/usr/bin/bash"}, "bash"), proc({}, "kworker/0:1")};
    CHECK_EQ(autoChatProfile({}, idle), "");
    CHECK_EQ(autoChatProfile({}, {}), "");

    std::vector<ProcessInfo> wow = idle;
    wow.push_back(proc({"C:\\Program Files (x86)\\World of Warcraft\\_classic_era_\\WowClassic.exe"}));
    CHECK_EQ(autoChatProfile({}, wow), "wow");
    for (const char* exe : {"Wow.exe", "WowB.exe", "WowT.exe", "WowClassicB.exe", "WowClassicT.exe"})
        CHECK_EQ(autoChatProfile({}, {proc({std::string("C:\\WoW\\") + exe})}), "wow");

    ChatProfiles custom;
    custom["ffxiv"].match = {"ffxiv_dx11.exe"};
    custom["forever"].match = {"_forever_/Wow.exe"};
    custom["forever"].channels = {{"say", "/s", {"say"}, false}};
    std::vector<ProcessInfo> ffxiv = {proc({"Z:\\games\\ffxiv_dx11.exe"})};
    CHECK_EQ(autoChatProfile(custom, ffxiv), "ffxiv");
    std::vector<ProcessInfo> forever = {proc({"C:\\World of Warcraft\\_forever_\\Wow.exe"})};
    CHECK_EQ(autoChatProfile(custom, forever), "forever");
    CHECK_EQ(autoChatProfile(custom, wow), "wow");

    std::vector<ProcessInfo> both = forever;
    both.push_back(ffxiv[0]);
    CHECK_EQ(autoChatProfile(custom, both), "ffxiv");

    ChatProfiles reordered;
    reordered["forever"] = custom["forever"];
    reordered["ffxiv"] = custom["ffxiv"];
    CHECK_EQ(autoChatProfile(reordered, both), "forever");

    ChatProfiles extra;
    extra["wow"].match = {"WowPTR.exe"};
    CHECK_EQ(autoChatProfile(extra, {proc({"C:\\WoW\\WowPTR.exe"})}), "wow");
    CHECK_EQ(autoChatProfile(extra, forever), "wow");
}

static void testReadUserProcesses() {
    bool found = false;
    for (const auto& p : readUserProcesses())
        if (processMatches(p, "test_chat_profile"))
            found = true;
    CHECK(found);
}

int main() {
    testEveryBuiltinChannel();
    testCaseAndPunctuation();
    testLongestAliasWins();
    testSlash();
    testWhisper();
    testNoMatchPassesThrough();
    testResolve();
    testProcessMatches();
    testAutoChatProfile();
    testReadUserProcesses();
    return checkReport();
}

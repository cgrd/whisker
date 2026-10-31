// SPDX-License-Identifier: MIT
#include "spawn.h"
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

std::vector<std::string> splitCommand(const std::string& command) {
    std::vector<std::string> args;
    std::string current;
    bool in_word = false;
    char quote = 0;
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (quote) {
            if (c == quote)
                quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < command.size())
                current += command[++i];
            else
                current += c;
        } else if (c == '\'' || c == '"') {
            quote = c;
            in_word = true;
        } else if (c == '\\' && i + 1 < command.size()) {
            current += command[++i];
            in_word = true;
        } else if (c == ' ' || c == '\t') {
            if (in_word) {
                args.push_back(current);
                current.clear();
                in_word = false;
            }
        } else {
            current += c;
            in_word = true;
        }
    }
    if (in_word)
        args.push_back(current);
    return args;
}

bool spawnDetached(const std::string& command) {
    std::vector<std::string> args = splitCommand(command);
    if (args.empty())
        return false;
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    // Double fork so the command is reparented to init and never becomes a zombie
    pid_t child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        setsid();
        if (fork() == 0) {
            execvp(argv[0], argv.data());
            perror(argv[0]);
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int runAndWait(const std::vector<std::string>& args) {
    if (args.empty())
        return -1;
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t child = fork();
    if (child < 0)
        return -1;
    if (child == 0) {
        execvp(argv[0], argv.data());
        perror(argv[0]);
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

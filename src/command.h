// SPDX-License-Identifier: MIT
#pragma once
#include <string>

enum class Command { None, Start, Stop, Capture };

class CommandSocket {
public:
    bool init(const std::string& path);
    void close();
    int fd() const { return server_fd_; }
    Command readCommand();
    void reply(const std::string& line);
private:
    int server_fd_ = -1;
    int client_fd_ = -1;
    std::string path_;
    std::string buf_;
};

// SPDX-License-Identifier: MIT
#pragma once
#include <string>

enum class Command { None, Start, Stop, Capture, Status };

// $XDG_RUNTIME_DIR if it is a real directory owned by this user and closed to everyone else;
// otherwise "" and error says why. Never falls back to a shared directory like /tmp, where
// another user could create the socket or lock first.
std::string runtimeDir(std::string& error);

// $XDG_RUNTIME_DIR/whisker.sock, or "" without a safe runtime directory.
std::string commandSocketPath();

// Sends one command line to a running daemon and stores its one-line reply. Returns false if no
// daemon listens; reply stays empty if the daemon doesn't answer within timeout_ms.
bool sendCommand(const std::string& path, const std::string& command, std::string& reply, int timeout_ms);

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
};

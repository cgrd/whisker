// SPDX-License-Identifier: MIT
#include "command.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>

bool CommandSocket::init(const std::string& path) {
    path_ = path;
    ::unlink(path.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd_ < 0) {
        fprintf(stderr, "command: socket failed\n");
        return false;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "command: bind failed: %s\n", path.c_str());
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    ::listen(server_fd_, 2);
    fprintf(stderr, "command: listening on %s\n", path.c_str());
    return true;
}

void CommandSocket::close() {
    if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
    if (server_fd_ >= 0) { ::close(server_fd_); server_fd_ = -1; }
    if (!path_.empty()) { ::unlink(path_.c_str()); path_.clear(); }
}

int CommandSocket::readCommand() {
    if (server_fd_ < 0) return 0;

    // Accept new connections
    int newfd = ::accept4(server_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    if (newfd >= 0) {
        if (client_fd_ >= 0) ::close(client_fd_);
        client_fd_ = newfd;
    }

    if (client_fd_ < 0) return 0;

    char tmp[128];
    ssize_t n = ::read(client_fd_, tmp, sizeof(tmp) - 1);
    if (n > 0) {
        tmp[n] = '\0';
        buf_ += tmp;
    } else if (n == 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }

    auto pos = buf_.find('\n');
    if (pos == std::string::npos) return 0;

    std::string cmd = buf_.substr(0, pos);
    buf_.erase(0, pos + 1);

    if (cmd == "start") return 1;
    if (cmd == "stop") return -1;
    return 0;
}

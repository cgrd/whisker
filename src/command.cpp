// SPDX-License-Identifier: MIT
#include "command.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr size_t kMaxCommand = 64;
static constexpr int kReadTimeoutUs = 500000;

bool CommandSocket::init(const std::string& path) {
    if (path.empty()) {
        std::string error;
        runtimeDir(error);
        fprintf(stderr, "command: no socket: %s\n", error.c_str());
        return false;
    }
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

// One command per connection. Only the listening socket is polled, so wait briefly for a
// client that connects first and writes a moment later (asyncio in the Decky plugin does).
Command CommandSocket::readCommand() {
    if (server_fd_ < 0) return Command::None;
    int fd = ::accept4(server_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return Command::None;
    if (client_fd_ >= 0) ::close(client_fd_);
    client_fd_ = fd;

    timeval timeout = {0, kReadTimeoutUs};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string line;
    char c;
    while (line.size() < kMaxCommand && ::read(fd, &c, 1) == 1 && c != '\n')
        line += c;

    if (line == "start") return Command::Start;
    if (line == "stop") return Command::Stop;
    if (line == "capture") return Command::Capture;
    if (line == "status") return Command::Status;
    return Command::None;
}

void CommandSocket::reply(const std::string& line) {
    if (client_fd_ < 0) return;
    std::string msg = line + "\n";
    if (::send(client_fd_, msg.data(), msg.size(), MSG_NOSIGNAL) < 0)
        fprintf(stderr, "command: reply failed: %s\n", strerror(errno));
}

std::string runtimeDir(std::string& error) {
    const char* env = getenv("XDG_RUNTIME_DIR");
    if (!env || env[0] != '/') {
        error = "XDG_RUNTIME_DIR is not set to an absolute path";
        return "";
    }
    std::string dir = env;
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        error = "cannot open XDG_RUNTIME_DIR " + dir + ": " + strerror(errno);
        if (fd >= 0) ::close(fd);
        return "";
    }
    ::close(fd);
    if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
        error = "XDG_RUNTIME_DIR " + dir + " must be owned by this user and closed to others (mode 0700)";
        return "";
    }
    error.clear();
    return dir;
}

std::string commandSocketPath() {
    std::string error;
    std::string dir = runtimeDir(error);
    return dir.empty() ? "" : dir + "/whisker.sock";
}

bool sendCommand(const std::string& path, const std::string& command, std::string& reply, int timeout_ms) {
    reply.clear();
    if (path.empty()) return false;
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    timeval timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string msg = command + "\n";
    std::string line;
    char c = 0;
    if (::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(msg.size())) {
        while (line.size() < kMaxCommand && ::read(fd, &c, 1) == 1 && c != '\n')
            line += c;
        if (c == '\n') reply = line;
    }
    ::close(fd);
    return true;
}

// SPDX-License-Identifier: MIT
#include "processes.h"
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static std::vector<std::string> splitNul(const std::string& data) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < data.size()) {
        size_t end = data.find('\0', start);
        if (end == std::string::npos) end = data.size();
        parts.push_back(data.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::vector<ProcessInfo> readUserProcesses() {
    std::vector<ProcessInfo> procs;
    DIR* dir = opendir("/proc");
    if (!dir)
        return procs;
    uid_t uid = getuid();
    while (dirent* entry = readdir(dir)) {
        if (!std::isdigit(static_cast<unsigned char>(entry->d_name[0])))
            continue;
        std::string base = std::string("/proc/") + entry->d_name;
        struct stat st;
        if (stat(base.c_str(), &st) != 0 || st.st_uid != uid)
            continue;
        std::string cmdline, comm;
        if (!readFile(base + "/cmdline", cmdline) || !readFile(base + "/comm", comm))
            continue;
        if (!comm.empty() && comm.back() == '\n')
            comm.pop_back();
        procs.push_back({splitNul(cmdline), comm});
    }
    closedir(dir);
    return procs;
}

// SPDX-License-Identifier: MIT
#include "models.h"
#include "sha256.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct ModelRow {
    const char* file;
    const char* quant;
    int bits;
    uint64_t size;
    const char* sha256;
    const char* note;
};
#include "models_data.h"
}

const std::vector<KnownModel>& knownModels() {
    static const std::vector<KnownModel> models = [] {
        std::vector<KnownModel> out;
        for (const auto& row : kModelRows)
            out.push_back({row.file, row.quant, row.bits, row.size, row.sha256, row.note});
        return out;
    }();
    return models;
}

std::string modelUrl(const KnownModel& model) {
    return std::string(kModelRepo) + "/resolve/" + kModelRevision + "/" + model.file;
}

int modelSizeMb(const KnownModel& model) {
    return static_cast<int>((model.size + 500000) / 1000000);
}

std::string modelLabel(const KnownModel& model) {
    return std::string(kModelName) + ", " + std::to_string(model.bits) + "-bit (" + model.quant + "), " +
           std::to_string(modelSizeMb(model)) + " MB";
}

bool modelIsRecommended(const KnownModel& model) { return model.file == kModelRecommended; }
std::string modelRecommendedReason() { return kModelRecommendedReason; }
std::string modelRepoUrl() { return kModelRepo; }
std::string modelOriginalUrl() { return kModelOriginal; }

std::string modelUserAgent() {
    return std::string("Whisker/") + kWhiskerVersion + " (+https://github.com/cgrd/whisker)";
}

// Runs curl with the shared safety options. curl retries a transient failure once and nothing
// loops around it. stdout goes to *out when given. Returns "" or an error.
static std::string runCurl(const std::vector<std::string>& args, std::string* out) {
    int err_pipe[2];
    int out_pipe[2] = {-1, -1};
    if (pipe2(err_pipe, O_CLOEXEC) != 0) return std::string("cannot start curl: ") + strerror(errno);
    if (out && pipe2(out_pipe, O_CLOEXEC) != 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        return std::string("cannot start curl: ") + strerror(errno);
    }
    std::string agent = modelUserAgent();
    std::vector<std::string> full = {"curl", "-fsS", "--retry", "1", "--proto-redir", "=https", "-A", agent};
    full.insert(full.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : full) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        dup2(err_pipe[1], STDERR_FILENO);
        if (out) dup2(out_pipe[1], STDOUT_FILENO);
        execvp("curl", argv.data());
        _exit(127);
    }
    close(err_pipe[1]);
    if (out) close(out_pipe[1]);
    std::string output;
    char buf[256];
    ssize_t n;
    while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) output.append(buf, n);
    close(err_pipe[0]);
    if (out) {
        out->clear();
        while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) out->append(buf, n);
        close(out_pipe[0]);
    }
    if (pid < 0) return std::string("cannot start curl: ") + strerror(errno);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return {};
    while (!output.empty() && (output.back() == '\n' || output.back() == ' ')) output.pop_back();
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) return "curl is not installed";
    if (output.empty()) return "curl failed";
    return output.substr(output.rfind('\n') + 1);
}

std::string downloadFile(const std::string& url, const std::string& path, uint64_t max_bytes) {
    std::vector<std::string> args = {"-L", "-o", path};
    if (max_bytes > 0) args.insert(args.end(), {"--max-filesize", std::to_string(max_bytes)});
    args.push_back(url);
    return runCurl(args, nullptr);
}

std::string fetchRedirect(const std::string& url, std::string& location) {
    return runCurl({"-o", "/dev/null", "-w", "%{redirect_url}", url}, &location);
}

std::string installVerified(const std::string& tmp, const std::string& dest, uint64_t size,
                            const std::string& sha256) {
    struct stat st;
    std::string error;
    if (stat(tmp.c_str(), &st) != 0) {
        error = "the download is missing";
    } else if (static_cast<uint64_t>(st.st_size) != size) {
        error = "the download has the wrong size (" + std::to_string(st.st_size) + " bytes, expected " +
                std::to_string(size) + ")";
    } else if (sha256File(tmp) != sha256) {
        error = "the download is corrupt (SHA-256 mismatch)";
    } else if (rename(tmp.c_str(), dest.c_str()) != 0) {
        error = std::string("cannot move the download into place: ") + strerror(errno);
    }
    if (!error.empty()) unlink(tmp.c_str());
    return error;
}

std::string downloadModel(const KnownModel& model, const std::string& dir) {
    std::string dest = dir + "/" + model.file;
    std::string tmp = dir + "/." + model.file + ".part";
    std::string error = downloadFile(modelUrl(model), tmp);
    if (!error.empty()) {
        unlink(tmp.c_str());
        return error;
    }
    return installVerified(tmp, dest, model.size, model.sha256);
}

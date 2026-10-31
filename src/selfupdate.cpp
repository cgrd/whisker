// SPDX-License-Identifier: MIT
#include "selfupdate.h"
#include "command.h"
#include "config.h"
#include "models.h"
#include "spawn.h"
#include "tray.h"
#include "update.h"

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const std::string kReleases = "https://github.com/cgrd/whisker/releases";
constexpr int kStatusTimeoutMs = 3000;
constexpr int kIdleWaitSeconds = 120;

int fail(const std::string& message) {
    fprintf(stderr, "whisker: update failed: %s\n", message.c_str());
    return 1;
}

struct TempDir {
    std::string path;

    TempDir() {
        const char* base = getenv("TMPDIR");
        std::string pattern = std::string(base && *base ? base : "/tmp") + "/whisker-update-XXXXXX";
        std::vector<char> buf(pattern.begin(), pattern.end());
        buf.push_back('\0');
        if (mkdtemp(buf.data())) path = buf.data();
    }

    ~TempDir() {
        std::error_code ec;
        if (!path.empty()) std::filesystem::remove_all(path, ec);
    }
};

std::string fetchSmall(const std::string& url, const std::string& path, uint64_t max_bytes, std::string& out) {
    std::string error = downloadFile(url, path, max_bytes, kCurlSmallMaxSeconds);
    if (!error.empty()) return error;
    std::ifstream f(path, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.size() > max_bytes) return "larger than " + std::to_string(max_bytes) + " bytes";
    return "";
}

enum class Daemon { Absent, Idle, Busy };

// A daemon that is transcribing doesn't read the socket, so no answer counts as busy too
Daemon daemonState() {
    std::string reply;
    if (!sendCommand(commandSocketPath(), "status", reply, kStatusTimeoutMs)) return Daemon::Absent;
    return reply == "idle" ? Daemon::Idle : Daemon::Busy;
}

bool waitUntilIdle() {
    for (int waited = 0; waited < kIdleWaitSeconds; waited += 2) {
        if (daemonState() != Daemon::Busy) return true;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return false;
}

bool serviceActive() {
    return runAndWait({"systemctl", "--user", "--quiet", "is-active", "whisker.service"}) == 0;
}

// Returns whether the service was restarted onto the new AppImage.
bool restartService(const std::string& tag) {
    if (!serviceActive()) return false;
    if (!waitUntilIdle()) {
        printf("Whisker is still busy; it switches to %s the next time it starts\n", tag.c_str());
        return false;
    }
    if (runAndWait({"systemctl", "--user", "restart", "whisker.service"}) != 0)
        fprintf(stderr, "whisker: could not restart the service; restart it to use %s\n", tag.c_str());
    return true;
}

std::vector<bool> watchService() {
    std::vector<bool> samples;
    for (int i = 0; i < kRestartWatchSeconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        samples.push_back(serviceActive());
    }
    return samples;
}

// A hard link costs nothing and keeps the old AppImage even after the rename replaces its name
std::string keepBackup(const std::string& appimage, const std::string& backup) {
    unlink(backup.c_str());
    if (link(appimage.c_str(), backup.c_str()) == 0) return "";
    std::string part = backup + ".part";
    std::error_code ec;
    std::filesystem::copy_file(appimage, part, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec && rename(part.c_str(), backup.c_str()) == 0) return "";
    unlink(part.c_str());
    return "cannot keep a copy of the current AppImage to roll back to: " + (ec ? ec.message() : strerror(errno));
}

std::string runningAppImage(std::string& path) {
    const char* env = getenv("APPIMAGE");
    char real[PATH_MAX];
    struct stat st;
    if (!env || !*env || !realpath(env, real) || stat(real, &st) != 0 || !S_ISREG(st.st_mode))
        return "Whisker is not running from an AppImage, so there is nothing to replace. "
               "Update a source build with git pull and make install, or reinstall with install.sh";
    path = real;
    std::string dir = path.substr(0, path.rfind('/'));
    if (access(dir.c_str(), W_OK) != 0) return "cannot write to " + dir + ": " + strerror(errno);
    return "";
}

// Two updates at once (timer and a manual run) would write the same .part file
std::string lockUpdates() {
    std::string error;
    std::string dir = runtimeDir(error);
    if (dir.empty()) return "cannot lock updates: " + error;
    std::string path = dir + "/whisker-update.lock";
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return "cannot open " + path + ": " + strerror(errno);
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) return "another update is already running";
    return "";
}

std::string installAppImage(const std::string& base_url, const Manifest& manifest, const std::string& appimage,
                            const std::string& tmp) {
    const FileDigest& zip = manifest.assets.at(kUpdateZipAsset);
    std::string zip_part = tmp + "/update.zip.part";
    std::string zip_path = tmp + "/update.zip";
    std::string error = downloadFile(base_url + kUpdateZipAsset, zip_part, zip.size, kCurlUpdateMaxSeconds);
    if (!error.empty()) {
        unlink(zip_part.c_str());
        return "download failed: " + error;
    }
    error = installVerified(zip_part, zip_path, zip.size, zip.sha256);
    if (!error.empty()) return error;

    size_t slash = appimage.rfind('/');
    std::string part = appimage.substr(0, slash + 1) + "." + appimage.substr(slash + 1) + ".part";
    error = extractStoredZipEntry(zip_path, kUpdateAppImageEntry, part, manifest.appimage.size);
    if (!error.empty()) return error;
    if (chmod(part.c_str(), 0755) != 0) {
        error = std::string("cannot make the update executable: ") + strerror(errno);
        unlink(part.c_str());
        return error;
    }
    std::string backup = appImageBackupPath(appimage);
    error = keepBackup(appimage, backup);
    if (error.empty()) error = installVerified(part, appimage, manifest.appimage.size, manifest.appimage.sha256);
    if (!error.empty()) {
        unlink(part.c_str());
        unlink(backup.c_str());
    }
    return error;
}

} // namespace

int runUpdate(UpdateMode mode, const std::string& config_path) {
    Config cfg = loadConfig(config_path);
    if (mode == UpdateMode::Scheduled && !cfg.auto_update) {
        printf("Automatic updates are off (auto_update: false)\n");
        return 0;
    }
    std::string appimage;
    if (mode != UpdateMode::Check) {
        std::string error = runningAppImage(appimage);
        if (!error.empty()) return fail(error);
        error = lockUpdates();
        if (!error.empty()) return fail(error);
    }
    if (!hasReleaseSigningKey())
        return fail("this build has no release signing key, so it cannot verify updates. Update with install.sh");

    std::string location;
    std::string error = fetchRedirect(kReleases + "/latest", location);
    if (!error.empty()) return fail("cannot check for updates: " + error);
    std::string tag = tagFromReleaseUrl(location);
    if (tag.empty()) return fail("unexpected answer from GitHub for the latest release: '" + location + "'");
    Version latest, running;
    if (!parseVersion(WHISKER_VERSION, running)) return fail(std::string("cannot read this version: ") + WHISKER_VERSION);
    parseVersion(tag, latest, true);
    if (latest <= running) {
        printf("Whisker %s is up to date\n", WHISKER_VERSION);
        return 0;
    }

    TempDir tmp;
    if (tmp.path.empty()) return fail(std::string("cannot create a temporary directory: ") + strerror(errno));
    std::string base_url = kReleases + "/download/" + tag + "/";
    std::string manifest_text, signature;
    error = fetchSmall(base_url + kManifestAsset, tmp.path + "/manifest", kMaxManifestBytes, manifest_text);
    if (error.empty())
        error = fetchSmall(base_url + kManifestAsset + ".minisig", tmp.path + "/manifest.minisig",
                           kMaxSignatureBytes, signature);
    if (!error.empty()) return fail("cannot download the release manifest for " + tag + ": " + error);

    std::string trusted_comment;
    error = verifyMinisign(manifest_text, signature, releaseSigningKeys(), trusted_comment);
    if (!error.empty()) return fail("the release manifest for " + tag + " is not trusted: " + error);
    Manifest manifest;
    error = parseManifest(manifest_text, manifest);
    if (error.empty()) error = checkManifest(manifest, tag, WHISKER_VERSION, trusted_comment);
    if (!error.empty()) return fail("refusing the release manifest for " + tag + ": " + error);

    if (mode == UpdateMode::Check) {
        printf("Whisker %s is available (this is %s). Install it with: whisker --update\n", tag.c_str(),
               WHISKER_VERSION);
        return 0;
    }
    if (daemonState() == Daemon::Busy) {
        if (mode == UpdateMode::Install) return fail("Whisker is busy recording; try again when it's done");
        printf("Whisker is busy recording; the update waits for the next run\n");
        return 0;
    }

    printf("Downloading Whisker %s\n", tag.c_str());
    error = installAppImage(base_url, manifest, appimage, tmp.path);
    if (!error.empty()) return fail(error);
    printf("Updated Whisker to %s\n", tag.c_str());

    std::string backup = appImageBackupPath(appimage);
    if (restartService(tag) && !restartSurvived(watchService())) {
        if (rename(backup.c_str(), appimage.c_str()) != 0)
            return fail("Whisker " + tag + " did not keep running after the restart, and restoring " +
                        WHISKER_VERSION + " failed: " + strerror(errno));
        runAndWait({"systemctl", "--user", "restart", "whisker.service"});
        std::string message = "Whisker " + tag + " did not keep running after the restart, so " + WHISKER_VERSION +
                              " is back. See journalctl --user -u whisker";
        if (cfg.tray) notifyDesktop("Whisker update rolled back", message);
        return fail(message);
    }
    unlink(backup.c_str());
    if (cfg.tray) notifyDesktop("Whisker updated", "Updated to " + tag + ". What's new: " + kReleases + "/tag/" + tag);
    return 0;
}

int runVerifyRelease(const std::string& dir) {
    auto failed = [](const std::string& message) {
        fprintf(stderr, "whisker: release verification failed: %s\n", message.c_str());
        return 1;
    };
    if (!hasReleaseSigningKey()) return failed("this build has no release signing key");
    std::string version;
    std::string error = verifyReleaseDir(dir, releaseSigningKeys(), WHISKER_VERSION, version);
    if (!error.empty()) return failed(error);
    printf("Verified Whisker v%s against its signed manifest\n", version.c_str());
    return 0;
}

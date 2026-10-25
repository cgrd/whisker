// SPDX-License-Identifier: MIT
#include "history.h"
#include "config.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <opusenc.h>

namespace {

void mkdirRecursive(const std::string& path) {
    std::string built;
    for (size_t i = 0; i < path.size(); ++i) {
        built += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            if (mkdir(built.c_str(), 0755) != 0 && errno != EEXIST)
                return;
        }
    }
}

std::string timestamp() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H-%M-%S", &tm);
    return buf;
}

} // namespace

History::History(int max_entries)
    : max_entries_(max_entries), dir_(historyDir()) {}

void History::save(const std::vector<float>& audio, const std::string& text) {
    if (max_entries_ <= 0)
        return;

    mkdirRecursive(dir_);

    std::string subdir = dir_ + "/" + timestamp();
    mkdir(subdir.c_str(), 0755);

    std::ofstream txt(subdir + "/text.txt");
    txt << text;

    writeOpus(subdir + "/audio.opus", audio);

    pruneOldEntries();
}

void History::writeOpus(const std::string& path, const std::vector<float>& audio) {
    OggOpusComments* comments = ope_comments_create();
    int err = 0;
    OggOpusEnc* enc = ope_encoder_create_file(
        path.c_str(), comments, 16000, 1, 0, &err);
    if (!enc) {
        fprintf(stderr, "history: failed to create opus encoder: %s\n",
                ope_strerror(err));
        ope_comments_destroy(comments);
        return;
    }

    ope_encoder_ctl(enc, OPUS_SET_BITRATE(64000));

    ope_encoder_write_float(enc, audio.data(),
                            static_cast<int>(audio.size()));
    ope_encoder_drain(enc);
    ope_encoder_destroy(enc);
    ope_comments_destroy(comments);
}

void History::pruneOldEntries() {
    DIR* d = opendir(dir_.c_str());
    if (!d)
        return;

    std::vector<std::string> entries;
    while (struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '.')
            continue;
        std::string full = dir_ + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            entries.push_back(ent->d_name);
    }
    closedir(d);

    if (static_cast<int>(entries.size()) <= max_entries_)
        return;

    std::sort(entries.begin(), entries.end());

    int to_remove = static_cast<int>(entries.size()) - max_entries_;
    for (int i = 0; i < to_remove; ++i) {
        std::string p = dir_ + "/" + entries[i];
        unlink((p + "/audio.opus").c_str());
        unlink((p + "/text.txt").c_str());
        rmdir(p.c_str());
    }
}

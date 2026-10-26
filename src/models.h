// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct KnownModel {
    std::string file;
    std::string quant;
    int bits;
    uint64_t size;
    std::string sha256;
    std::string note;
};

// The downloadable models, in models.json order (most accurate first).
const std::vector<KnownModel>& knownModels();

// Pinned to the Hugging Face commit in models.json, so the hash and size always match.
std::string modelUrl(const KnownModel& model);
int modelSizeMb(const KnownModel& model);
// "Parakeet 0.6B, 8-bit (Q8_0), 731 MB"
std::string modelLabel(const KnownModel& model);
// Shown as information next to one entry; never preselected or downloaded on its own.
bool modelIsRecommended(const KnownModel& model);
std::string modelRecommendedReason();
std::string modelRepoUrl();
std::string modelOriginalUrl();

// "Whisker/<version> (+https://github.com/cgrd/whisker)", sent with every model download.
std::string modelUserAgent();

// Fetches url to path with curl. curl retries a transient failure once and nothing loops
// around it, so a failed download stops until the user starts it again. Returns "" or an error.
std::string downloadFile(const std::string& url, const std::string& path);

// Moves a finished download into place only if its size and SHA-256 match; otherwise deletes
// it. Returns "" or an error for the user.
std::string installVerified(const std::string& tmp, const std::string& dest, uint64_t size,
                            const std::string& sha256);

// Downloads a model to dir via a temporary file and verifies it. Returns "" or an error.
std::string downloadModel(const KnownModel& model, const std::string& dir);

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
    std::string note;
};

// The downloadable models, in models.json order (most accurate first).
const std::vector<KnownModel>& knownModels();

std::string modelUrl(const KnownModel& model);
int modelSizeMb(const KnownModel& model);
// "Parakeet 0.6B, 8-bit (Q8_0), 731 MB"
std::string modelLabel(const KnownModel& model);
// Shown as information next to one entry; never preselected or downloaded on its own.
bool modelIsRecommended(const KnownModel& model);
std::string modelRecommendedReason();
std::string modelRepoUrl();
std::string modelOriginalUrl();

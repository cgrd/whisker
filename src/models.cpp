// SPDX-License-Identifier: MIT
#include "models.h"

namespace {
struct ModelRow {
    const char* file;
    const char* quant;
    int bits;
    uint64_t size;
    const char* note;
};
#include "models_data.h"
}

const std::vector<KnownModel>& knownModels() {
    static const std::vector<KnownModel> models = [] {
        std::vector<KnownModel> out;
        for (const auto& row : kModelRows)
            out.push_back({row.file, row.quant, row.bits, row.size, row.note});
        return out;
    }();
    return models;
}

std::string modelUrl(const KnownModel& model) {
    return std::string(kModelRepo) + "/resolve/main/" + model.file;
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

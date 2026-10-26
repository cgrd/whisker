// SPDX-License-Identifier: MIT
#include "check.h"
#include "models.h"

int main() {
    const auto& models = knownModels();
    CHECK_EQ(models.size(), size_t(4));
    CHECK_EQ(modelLabel(models[0]), std::string("Parakeet 0.6B, 8-bit (Q8_0), 731 MB"));
    CHECK_EQ(modelLabel(models[3]), std::string("Parakeet 0.6B, 4-bit (Q4_K_M), 477 MB"));
    CHECK_EQ(modelUrl(models[0]),
             std::string("https://huggingface.co/handy-computer/parakeet-unified-en-0.6b-gguf/resolve/main/"
                         "parakeet-unified-en-0.6b-Q8_0.gguf"));
    for (size_t i = 1; i < models.size(); ++i) {
        CHECK(models[i].size < models[i - 1].size);
        CHECK(models[i].bits < models[i - 1].bits);
        CHECK(!models[i].note.empty());
    }
    CHECK_EQ(modelOriginalUrl(), std::string("https://huggingface.co/nvidia/parakeet-unified-en-0.6b"));
    return checkReport();
}

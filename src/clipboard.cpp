// SPDX-License-Identifier: MIT
#include "clipboard.h"
#include <cstdio>

bool clipboardCopy(const std::string& text) {
    FILE* pipe = popen("wl-copy", "w");
    if (!pipe) {
        fprintf(stderr, "clipboard: failed to run wl-copy\n");
        return false;
    }

    fwrite(text.data(), 1, text.size(), pipe);

    if (pclose(pipe) != 0) {
        fprintf(stderr, "clipboard: wl-copy failed\n");
        return false;
    }

    return true;
}

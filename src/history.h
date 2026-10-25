// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

class History {
public:
    explicit History(int max_entries);

    void save(const std::vector<float>& audio, const std::string& text);

private:
    int max_entries_;
    std::string dir_;

    void pruneOldEntries();
    void writeOpus(const std::string& path, const std::vector<float>& audio);
};

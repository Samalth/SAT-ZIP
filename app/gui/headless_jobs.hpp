#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace szgui {

struct HeadlessJob {
    std::atomic<size_t> done{0}, total{0};
    std::atomic<bool> cancel{false}, finished{false};
    bool create = false;
    int files = 0, bad = 0;
    uint64_t raw = 0, packed = 0;
    std::string error;
    std::vector<std::string> failed;
    std::filesystem::path dest;
};


void headless_zip_job(const char* path, HeadlessJob& j);
void headless_extract_job(const char* archive, bool subfolder, HeadlessJob& j);

}

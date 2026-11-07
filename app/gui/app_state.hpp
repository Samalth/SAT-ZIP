#pragma once

#include "satzip_ops.hpp"
#include "satzip_tar.hpp"
#include "satzip_bench.h"
#include "satzip_obelisk.hpp"
#include "satzip_settings.hpp"
#include "archive_tree.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <filesystem>

struct GLFWwindow;

namespace szgui {

namespace fs = std::filesystem;

enum class Mode { Empty, Building, Viewing };

struct Series { std::vector<float> x, y; void clear() { x.clear(); y.clear(); } };
struct CodecView { const char* name = ""; Series comp, decomp; sz_bench_result result{}; bool done = false; };

enum class AKind { Satz, Zip, SevenZ, Tar };

struct App {
    GLFWwindow* win = nullptr;
    std::mutex  mtx;
    std::atomic<bool> running{false};
    std::atomic<size_t> prog_done{0}, prog_total{0};
    std::thread worker;
    std::string status = "Ready.";

    Mode mode = Mode::Empty;
    std::vector<sz::Item> staged;
    std::string suggest_name;
    sz::Archive opened;
    AKind okind = AKind::Satz;
    bool opened_readonly = false;
    std::string opened_path;
    std::vector<uint8_t> tarbuf;
    std::vector<sz::TarEntry> tentries;
    std::vector<ViewEntry> ventries;
    uint64_t ventries_rev = 0;
    ArchiveTree tree;
    std::atomic<bool> cancel{false};
    std::atomic<bool> share_pending{false};
    std::string share_path;
    fs::path obelisk = sz::find_obelisk();
    bool obelisk_after = false;
    bool mail_after = false;
    sz::Settings settings{};

    std::vector<uint8_t> bench_data;
    std::string bench_source = "(none)";
    CodecView sat_s, sat_r, sat_t, lz4, zipdef, lzma;
    bool show_s = true, show_r = true, show_t = true, show_lz4 = true, show_zip = true, show_lzma = true;
    bool skip_hidden = false;
    int block_kib = 64, runs = 25, bench_level = 9, bench_threads = 0;
    sz::Settings settings_saved{};

    ~App() { if (worker.joinable()) worker.join(); }
};

inline void join_worker(App* a) { if (a->worker.joinable()) a->worker.join(); }
inline void launch(App* a, std::thread&& t) { join_worker(a); a->cancel = false; a->worker = std::move(t); }

}

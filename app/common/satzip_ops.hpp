#pragma once

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

#include "satzip_stream.h"
#include "satzip_archive.h"

#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <vector>
#include <string>
#include <functional>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace sz {
namespace fs = std::filesystem;

using Progress = std::function<void(size_t done, size_t total)>;

struct Item { fs::path abs; std::string name; bool is_dir; uintmax_t size; int64_t mtime = SZ_AR_NO_MTIME; };

inline int64_t to_unix_ns(fs::file_time_type t) {
#if defined(_MSC_VER)
    return ((int64_t)t.time_since_epoch().count() - 116444736000000000LL) * 100;
#else
    auto sys = std::chrono::system_clock::now() + (t - fs::file_time_type::clock::now());
    return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(sys.time_since_epoch()).count();
#endif
}

inline fs::file_time_type from_unix_ns(int64_t ns) {
#if defined(_MSC_VER)
    return fs::file_time_type(fs::file_time_type::duration(ns / 100 + 116444736000000000LL));
#else
    auto sys = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(ns)));
    return fs::file_time_type::clock::now() + (sys - std::chrono::system_clock::now());
#endif
}

inline int64_t file_mtime(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    return ec ? SZ_AR_NO_MTIME : to_unix_ns(t);
}

inline void restore_mtime(const fs::path& p, int64_t ns) {
    if (ns == SZ_AR_NO_MTIME) return;
    std::error_code ec;
    fs::last_write_time(p, from_unix_ns(ns), ec);
}

inline std::vector<uint8_t> read_file(const fs::path& p, size_t real, size_t pad) {
    std::vector<uint8_t> buf(real + pad);
    std::ifstream f(p, std::ios::binary);
    if (real) f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)real);
    if (pad) std::memset(buf.data() + real, 0, pad);
    return buf;
}
inline bool write_file(const fs::path& p, const uint8_t* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    if (n) f.write(reinterpret_cast<const char*>(d), (std::streamsize)n);
    return (bool)f;
}

inline bool write_file_atomic(const fs::path& p, const uint8_t* d, size_t n) {
    fs::path tmp = p;
    tmp += ".sztmp";
#ifdef _WIN32
    FILE* f = _wfopen(tmp.c_str(), L"wbc");
#else
    FILE* f = std::fopen(tmp.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = (n == 0 || std::fwrite(d, 1, n, f) == n) && std::fflush(f) == 0;
#ifndef _WIN32
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) { fs::rename(tmp, p, ec); ok = !ec; }
    if (!ok) fs::remove(tmp, ec);
    return ok;
}

inline void gather(const fs::path& input, std::vector<Item>& out) {
    std::error_code ec;
    fs::path root = fs::absolute(input, ec).lexically_normal();
    if (!root.has_filename() && root.has_relative_path()) root = root.parent_path();
    if (fs::is_directory(root, ec)) {
        fs::path base = root.parent_path();
        out.push_back({root, root.lexically_relative(base).generic_string(), true, 0, file_mtime(root)});
        for (auto it = fs::recursive_directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path& p = it->path();
            bool dir = it->is_directory(ec);
            uintmax_t s = dir ? 0 : it->file_size(ec);
            if (ec) s = 0;
            auto t = it->last_write_time(ec);
            out.push_back({p, p.lexically_relative(base).generic_string(), dir, s,
                           ec ? SZ_AR_NO_MTIME : to_unix_ns(t)});
        }
    } else {
        out.push_back({root, root.filename().generic_string(), false, fs::file_size(root, ec), file_mtime(root)});
    }
}

inline bool is_precompressed(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = name.substr(dot + 1);
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    static const char* exts[] = {
        "jpg","jpeg","png","gif","webp","avif","bmp","heic","cr2","nef","arw",
        "mp3","aac","ogg","flac","opus","m4a","wma",
        "mp4","mkv","mov","avi","webm","m4v","wmv","mpg","mpeg",
        "zip","7z","gz","bz2","xz","zst","rar","satz","jar","tgz","br","lz4","cab","iso",
        "obl","apk","msi","dmg","pkg","crx","nupkg","whl",
        "woff","woff2","docx","xlsx","pptx","odt","epub","pdf"
    };
    for (auto x : exts) if (e == x) return true;
    return false;
}

inline int clamp_threads(int nthreads, size_t work) {
    unsigned hw = std::thread::hardware_concurrency();
    if (!hw) hw = 4;
    if (nthreads <= 0) nthreads = (int)hw;
    if ((unsigned)nthreads > hw) nthreads = (int)hw;
    if (work && (size_t)nthreads > work) nthreads = (int)work;
    if (nthreads < 1) nthreads = 1;
    return nthreads;
}

template <class F>
inline void parallel_for(size_t count, int nthreads, F&& fn, const std::atomic<bool>* cancel = nullptr) {
    if (count == 0) return;
    nthreads = clamp_threads(nthreads, count);
    std::atomic<size_t> next{0};
    auto worker = [&]{
        size_t i;
        while ((i = next.fetch_add(1)) < count) {
            if (cancel && cancel->load(std::memory_order_relaxed)) break;
            fn(i);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < nthreads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& th : pool) th.join();
}

struct Blob { uint8_t method = 0; uint32_t crc = 0; uint64_t orig = 0; std::vector<uint8_t> payload; };

inline size_t build_archive(const std::vector<Item>& items, std::vector<uint8_t>& out,
                            int acceleration, int nthreads, bool compress_all, int mode,
                            const Progress& progress = {}, const std::atomic<bool>* cancel = nullptr) {
    size_t N = items.size();
    std::vector<Blob> blobs(N);
    std::atomic<size_t> done{0};

    parallel_for(N, nthreads, [&](size_t i) {
        const Item& it = items[i];
        Blob& b = blobs[i];
        if (!it.is_dir) {
            auto data = read_file(it.abs, (size_t)it.size, SZ_MARGIN);
            b.orig = it.size;
            b.crc  = sz_crc32(data.data(), (size_t)it.size);
            bool skip = !compress_all && is_precompressed(it.name);
            if (!skip) {
                std::vector<uint8_t> p(sz_stream_bound((size_t)it.size, 16));
                sz_state_t st{};
                size_t c = sz_stream_compress(&st, data.data(), (size_t)it.size,
                                              p.data(), p.size(), 16, acceleration, mode);
                if (c && c < it.size) { p.resize(c); b.method = 1; b.payload = std::move(p); }
            }
            if (b.method == 0) b.payload.assign(data.begin(), data.begin() + (size_t)it.size);
        }
        if (progress) progress(done.fetch_add(1) + 1, N);
    }, cancel);
    if (cancel && cancel->load()) { out.clear(); return 0; }

    size_t bound = SZ_AR_HEADER + SZ_AR_END + SZ_MARGIN;
    for (size_t i = 0; i < N; ++i)
        bound += SZ_AR_DIR_BOUND + items[i].name.size() + blobs[i].payload.size();
    out.assign(bound, 0);

    sz_archive_writer w;
    if (sz_aw_init(&w, out.data(), out.size()) != 0) { out.clear(); return 0; }
    for (size_t i = 0; i < N; ++i) {
        const Item& it = items[i];
        if (it.is_dir) sz_aw_add_blob(&w, it.name.c_str(), 1, 0, 0, 0, nullptr, 0);
        else sz_aw_add_blob(&w, it.name.c_str(), 0, blobs[i].method, blobs[i].crc,
                            blobs[i].orig, blobs[i].payload.data(), blobs[i].payload.size());
        sz_aw_set_mtime(&w, it.mtime);
    }
    size_t n = sz_aw_finish(&w);
    out.resize(n);
    return n;
}

struct Archive {
    std::vector<uint8_t> buf;
    sz_ar_entry*         entries = nullptr;
    uint32_t             count   = 0;
    void close() { if (entries) { sz_ar_close(entries, count); entries = nullptr; count = 0; } }
    ~Archive() { close(); }
};

inline std::string legacy_to_utf8(const std::string& s, bool oem) {
#ifdef _WIN32
    if (s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0))
        return s;
    auto system_cp = [](LCTYPE type) {
        DWORD cp = 0;
        return GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, type | LOCALE_RETURN_NUMBER,
                               (LPWSTR)&cp, sizeof(cp) / sizeof(WCHAR)) && cp ? (UINT)cp : 0u;
    };
    static const UINT oem_cp  = system_cp(LOCALE_IDEFAULTCODEPAGE);
    static const UINT ansi_cp = system_cp(LOCALE_IDEFAULTANSICODEPAGE);
    UINT cp = oem ? (oem_cp ? oem_cp : 437u) : (ansi_cp ? ansi_cp : 1252u);
    int wn = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)un, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, &out[0], un, nullptr, nullptr);
    return out;
#else
    (void)oem;
    return s;
#endif
}

inline bool open_archive(const fs::path& p, Archive& a) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (ec) return false;
    a.close();
    a.buf = read_file(p, sz, 0);
    if (sz_ar_open(a.buf.data(), a.buf.size(), &a.entries, &a.count) != 0) {
        a.entries = nullptr; a.count = 0; return false;
    }
    for (uint32_t i = 0; i < a.count; ++i) {
        std::string fixed = legacy_to_utf8(a.entries[i].name, false);
        if (fixed == a.entries[i].name) continue;
        char* n = (char*)malloc(fixed.size() + 1);
        if (!n) continue;
        memcpy(n, fixed.c_str(), fixed.size() + 1);
        free(a.entries[i].name);
        a.entries[i].name = n;
    }
    return true;
}

inline bool safe_rel(const fs::path& rel) {
    if (rel.is_absolute() || rel.has_root_name()) return false;
    for (const auto& part : rel) if (part == "..") return false;
    return true;
}

inline int extract(const Archive& a, const fs::path& destdir, const std::vector<int>* sel,
                   int nthreads, const Progress& progress = {}, int* failures = nullptr,
                   const std::atomic<bool>* cancel = nullptr,
                   std::vector<std::string>* failed_names = nullptr) {
    std::vector<uint32_t> idx;
    if (sel) { idx.reserve(sel->size()); for (int s : *sel) idx.push_back((uint32_t)s); }
    else     { idx.reserve(a.count);     for (uint32_t i = 0; i < a.count; ++i) idx.push_back(i); }

    std::atomic<int>    files{0}, bad{0};
    std::atomic<size_t> done{0};
    std::mutex fmtx;
    size_t total = idx.size();

    auto fail = [&](const char* name) {
        bad.fetch_add(1);
        if (failed_names) { std::lock_guard<std::mutex> lk(fmtx); failed_names->push_back(name); }
    };

    parallel_for(total, nthreads, [&](size_t k) {
        const sz_ar_entry& e = a.entries[idx[k]];
        std::error_code ec;
        fs::path rel = fs::path(e.name).lexically_normal();
        if (safe_rel(rel)) {
            fs::path dst = destdir / rel;
            if (e.is_dir) {
                fs::create_directories(dst, ec);
            } else {
                fs::create_directories(dst.parent_path(), ec);
                std::vector<uint8_t> obuf((size_t)e.orig_size + SZ_MARGIN);
                size_t d = sz_ar_extract(a.buf.data(), a.buf.size(), &e, obuf.data(), (size_t)e.orig_size);
                if (d == SZ_ERR || !write_file(dst, obuf.data(), d)) fail(e.name);
                else { restore_mtime(dst, e.mtime); files.fetch_add(1); }
            }
        } else {
            fail(e.name);
        }
        if (progress) progress(done.fetch_add(1) + 1, total);
    }, cancel);

    for (uint32_t i : idx) {
        const sz_ar_entry& e = a.entries[i];
        fs::path rel = fs::path(e.name).lexically_normal();
        if (e.is_dir && safe_rel(rel)) restore_mtime(destdir / rel, e.mtime);
    }
    if (failures) *failures = bad.load();
    return files.load();
}

} // namespace sz

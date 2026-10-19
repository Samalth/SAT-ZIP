#pragma once

#include "satzip_ops.hpp"

#include <cstdlib>

namespace sz {

namespace tar {

inline void octal(char* dst, size_t width, uint64_t v) {
    for (size_t i = width; i-- > 1;) { dst[i - 1] = (char)('0' + (v & 7)); v >>= 3; }
    dst[width - 1] = 0;
}

inline void block(std::vector<uint8_t>& out, const std::string& name, const std::string& prefix, char type,
                  uint64_t mode, uint64_t size, uint64_t mtime_s) {
    char h[512];
    memset(h, 0, sizeof(h));
    memcpy(h, name.data(), name.size() < 100 ? name.size() : 100);
    octal(h + 100, 8, mode);
    octal(h + 108, 8, 0);
    octal(h + 116, 8, 0);
    octal(h + 124, 12, size);
    octal(h + 136, 12, mtime_s);
    memset(h + 148, ' ', 8);
    h[156] = type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memcpy(h + 345, prefix.data(), prefix.size() < 155 ? prefix.size() : 155);
    unsigned sum = 0;
    for (int i = 0; i < 512; ++i) sum += (unsigned char)h[i];
    octal(h + 148, 7, sum);
    h[155] = ' ';
    out.insert(out.end(), h, h + 512);
}

inline void pax_record(std::string& rec, const char* key, const std::string& val) {
    size_t base = strlen(key) + val.size() + 3;
    size_t len = base + 1;
    while (std::to_string(len).size() + base != len) len = std::to_string(len).size() + base;
    rec += std::to_string(len) + " " + key + "=" + val + "\n";
}

inline bool header(std::vector<uint8_t>& out, const std::string& name, bool is_dir, uint64_t size, int64_t mtime) {
    std::string n = name;
    if (is_dir && (n.empty() || n.back() != '/')) n += '/';
    uint64_t mt = mtime == SZ_AR_NO_MTIME || mtime < 0 ? 0 : (uint64_t)(mtime / 1000000000);
    std::string base = n, prefix;
    bool split_ok = true;
    if (n.size() > 100) {
        size_t cut = n.rfind('/', 154);
        split_ok = cut != std::string::npos && cut <= 154 && n.size() - cut - 1 <= 100;
        if (split_ok) { prefix = n.substr(0, cut); base = n.substr(cut + 1); }
    }
    bool ascii = true;
    for (char c : n) if ((unsigned char)c >= 0x80) { ascii = false; break; }
    if (!ascii || !split_ok) {
        std::string rec;
        pax_record(rec, "path", n);
        block(out, "PaxHeader", "", 'x', 0644, rec.size(), mt);
        out.insert(out.end(), rec.begin(), rec.end());
        out.insert(out.end(), (512 - rec.size() % 512) % 512, 0);
        if (!split_ok) { prefix.clear(); base = n.substr(0, 100); }
    }
    block(out, base, prefix, is_dir ? '5' : '0', is_dir ? 0755 : 0644, is_dir ? 0 : size, mt);
    return true;
}

inline uint64_t rd_octal(const char* p, size_t w) {
    uint64_t v = 0;
    for (size_t i = 0; i < w && p[i]; ++i) {
        if (p[i] == ' ') continue;
        if (p[i] < '0' || p[i] > '7') break;
        v = (v << 3) | (uint64_t)(p[i] - '0');
    }
    return v;
}

}

struct TarEntry { std::string name; bool is_dir; uint64_t size; size_t offset; int64_t mtime = SZ_AR_NO_MTIME; };

inline bool is_tar_data(const uint8_t* p, size_t n) {
    return n >= 512 && memcmp(p + 257, "ustar", 5) == 0;
}

inline bool tar_list(const uint8_t* buf, size_t n, std::vector<TarEntry>& out) {
    out.clear();
    size_t off = 0;
    std::string pending_long, pax_path;
    int64_t pax_mtime = SZ_AR_NO_MTIME;
    while (off + 512 <= n) {
        const char* h = (const char*)buf + off;
        bool empty = true;
        for (int i = 0; i < 512 && empty; ++i) if (h[i]) empty = false;
        if (empty) break;

        uint64_t size = tar::rd_octal(h + 124, 12);
        char type = h[156];
        std::string name;
        if (!pending_long.empty()) { name = pending_long; pending_long.clear(); }
        else {
            char prefix[156] = {0}, base[101] = {0};
            memcpy(base, h, 100);
            if (memcmp(h + 257, "ustar", 5) == 0) memcpy(prefix, h + 345, 155);
            name = prefix[0] ? std::string(prefix) + "/" + base : std::string(base);
        }
        size_t data_off = off + 512;
        size_t padded = ((size_t)size + 511) & ~(size_t)511;
        if (data_off + padded > n && !(size == 0)) return false;

        if (type == 'L') {
            if (data_off + size > n) return false;
            pending_long.assign((const char*)buf + data_off, (size_t)size);
            while (!pending_long.empty() && pending_long.back() == '\0') pending_long.pop_back();
        } else if (type == 'x') {
            if (data_off + size > n) return false;
            const char* p = (const char*)buf + data_off;
            const char* e = p + size;
            while (p < e) {
                char* sp = nullptr;
                unsigned long long len = strtoull(p, &sp, 10);
                if (len == 0 || sp >= e || *sp != ' ' || (size_t)len > (size_t)(e - p)) break;
                std::string kv((const char*)sp + 1, p + len - 1);
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string key = kv.substr(0, eq), val = kv.substr(eq + 1);
                    if (key == "path") pax_path = val;
                    else if (key == "mtime") {
                        size_t dot = val.find('.');
                        int64_t ns = strtoll(val.c_str(), nullptr, 10) * 1000000000;
                        if (dot != std::string::npos) {
                            std::string frac = (val.substr(dot + 1) + "000000000").substr(0, 9);
                            ns += (val[0] == '-' ? -1 : 1) * strtoll(frac.c_str(), nullptr, 10);
                        }
                        pax_mtime = ns;
                    }
                }
                p += len;
            }
        } else if (type == '0' || type == '\0' || type == '5') {
            name = pax_path.empty() ? legacy_to_utf8(name, false) : pax_path;
            bool dir = type == '5' || (!name.empty() && name.back() == '/');
            if (dir && !name.empty() && name.back() == '/') name.pop_back();
            int64_t mt = pax_mtime != SZ_AR_NO_MTIME ? pax_mtime : (int64_t)tar::rd_octal(h + 136, 12) * 1000000000;
            if (!name.empty()) out.push_back({ name, dir, size, data_off, mt });
            pax_path.clear();
            pax_mtime = SZ_AR_NO_MTIME;
        }
        off = data_off + padded;
    }
    return !out.empty();
}

inline int tar_extract(const uint8_t* buf, size_t n, const std::vector<TarEntry>& entries,
                       const fs::path& dest, const std::vector<int>* sel,
                       const Progress& progress = {}, int* failures = nullptr,
                       const std::atomic<bool>* cancel = nullptr,
                       std::vector<std::string>* failed_names = nullptr) {
    std::vector<uint32_t> idx;
    if (sel) { for (int s : *sel) idx.push_back((uint32_t)s); }
    else     { for (uint32_t i = 0; i < entries.size(); ++i) idx.push_back(i); }

    int files = 0, bad = 0;
    size_t done = 0;
    std::error_code ec;
    std::vector<std::pair<fs::path, int64_t>> dir_times;
    for (uint32_t i : idx) {
        if (cancel && *cancel) break;
        if (i >= entries.size()) { ++bad; continue; }
        const TarEntry& e = entries[i];
        bool ok = false;
        fs::path rel = fs::path(e.name).lexically_normal();
        if (safe_rel(rel) && e.offset + e.size <= n) {
            fs::path dst = dest / rel;
            if (e.is_dir) { fs::create_directories(dst, ec); dir_times.push_back({ dst, e.mtime }); ok = true; }
            else {
                fs::create_directories(dst.parent_path(), ec);
                ok = write_file(dst, buf + e.offset, (size_t)e.size);
                if (ok) { restore_mtime(dst, e.mtime); ++files; }
            }
        }
        if (!ok) { ++bad; if (failed_names) failed_names->push_back(e.name); }
        if (progress) progress(++done, idx.size());
    }
    for (auto& d : dir_times) restore_mtime(d.first, d.second);
    if (failures) *failures = bad;
    return files;
}

inline size_t build_tar(const std::vector<Item>& items, std::vector<uint8_t>& out,
                        const Progress& progress = {}, int* failures = nullptr) {
    out.clear();
    size_t done = 0;
    int bad = 0;
    for (const auto& it : items) {
        if (!tar::header(out, it.name, it.is_dir, it.size, it.mtime)) { ++bad; }
        else if (!it.is_dir) {
            auto data = read_file(it.abs, (size_t)it.size, 0);
            out.insert(out.end(), data.begin(), data.end());
            size_t pad = (512 - (it.size % 512)) % 512;
            out.insert(out.end(), pad, 0);
        }
        if (progress) progress(++done, items.size());
    }
    out.insert(out.end(), 1024, 0);
    if (failures) *failures = bad;
    return out.size();
}

}

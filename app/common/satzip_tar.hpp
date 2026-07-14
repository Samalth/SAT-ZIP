#pragma once

#include "satzip_ops.hpp"

namespace sz {

namespace tar {

inline void octal(char* dst, size_t width, uint64_t v) {
    for (size_t i = width; i-- > 1;) { dst[i - 1] = (char)('0' + (v & 7)); v >>= 3; }
    dst[width - 1] = 0;
}

inline bool header(std::vector<uint8_t>& out, const std::string& name, bool is_dir, uint64_t size) {
    std::string n = name;
    if (is_dir && (n.empty() || n.back() != '/')) n += '/';
    char h[512];
    memset(h, 0, sizeof(h));
    std::string prefix;
    if (n.size() > 100) {
        size_t cut = n.rfind('/', 154);
        if (cut == std::string::npos || n.size() - cut - 1 > 100 || cut > 154) return false;
        prefix = n.substr(0, cut);
        n = n.substr(cut + 1);
        if (n.size() > 100) return false;
    }
    memcpy(h, n.c_str(), n.size());
    octal(h + 100, 8, is_dir ? 0755 : 0644);
    octal(h + 108, 8, 0);
    octal(h + 116, 8, 0);
    octal(h + 124, 12, is_dir ? 0 : size);
    octal(h + 136, 12, 0);
    memset(h + 148, ' ', 8);
    h[156] = is_dir ? '5' : '0';
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memcpy(h + 345, prefix.c_str(), prefix.size());
    unsigned sum = 0;
    for (int i = 0; i < 512; ++i) sum += (unsigned char)h[i];
    octal(h + 148, 7, sum);
    h[155] = ' ';
    out.insert(out.end(), h, h + 512);
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

struct TarEntry { std::string name; bool is_dir; uint64_t size; size_t offset; };

inline bool is_tar_data(const uint8_t* p, size_t n) {
    return n >= 512 && memcmp(p + 257, "ustar", 5) == 0;
}

inline bool tar_list(const uint8_t* buf, size_t n, std::vector<TarEntry>& out) {
    out.clear();
    size_t off = 0;
    std::string pending_long;
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
        } else if (type == '0' || type == '\0' || type == '5') {
            bool dir = type == '5' || (!name.empty() && name.back() == '/');
            if (dir && !name.empty() && name.back() == '/') name.pop_back();
            if (!name.empty()) out.push_back({ name, dir, size, data_off });
        }
        off = data_off + padded;
    }
    return !out.empty();
}

inline int tar_extract(const uint8_t* buf, size_t n, const std::vector<TarEntry>& entries,
                       const fs::path& dest, const std::vector<int>* sel,
                       const Progress& progress = {}, int* failures = nullptr,
                       std::vector<std::string>* failed_names = nullptr) {
    std::vector<uint32_t> idx;
    if (sel) { for (int s : *sel) idx.push_back((uint32_t)s); }
    else     { for (uint32_t i = 0; i < entries.size(); ++i) idx.push_back(i); }

    int files = 0, bad = 0;
    size_t done = 0;
    std::error_code ec;
    for (uint32_t i : idx) {
        if (i >= entries.size()) { ++bad; continue; }
        const TarEntry& e = entries[i];
        bool ok = false;
        fs::path rel = fs::path(e.name).lexically_normal();
        if (safe_rel(rel) && e.offset + e.size <= n) {
            fs::path dst = dest / rel;
            if (e.is_dir) { fs::create_directories(dst, ec); ok = true; }
            else {
                fs::create_directories(dst.parent_path(), ec);
                ok = write_file(dst, buf + e.offset, (size_t)e.size);
                if (ok) ++files;
            }
        }
        if (!ok) { ++bad; if (failed_names) failed_names->push_back(e.name); }
        if (progress) progress(++done, idx.size());
    }
    if (failures) *failures = bad;
    return files;
}

inline size_t build_tar(const std::vector<Item>& items, std::vector<uint8_t>& out,
                        const Progress& progress = {}, int* failures = nullptr) {
    out.clear();
    size_t done = 0;
    int bad = 0;
    for (const auto& it : items) {
        if (!tar::header(out, it.name, it.is_dir, it.size)) { ++bad; }
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

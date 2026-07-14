#pragma once

#include "satzip_ops.hpp"

#if defined(SATZIP_HAVE_ZIP)
#include "miniz.h"

namespace sz {

struct ZipEntry { std::string name; bool is_dir; uint64_t orig, stored; uint32_t idx; };

inline bool is_zip_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char m[4] = {0};
    f.read(m, 4);
    return f.gcount() == 4 && m[0] == 'P' && m[1] == 'K' && (m[2] == 3 || m[2] == 5 || m[2] == 7);
}

inline bool zip_list(const fs::path& p, std::vector<ZipEntry>& out) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    if (ec) return false;
    auto buf = read_file(p, (size_t)n, 0);
    mz_zip_archive za;
    memset(&za, 0, sizeof(za));
    if (!mz_zip_reader_init_mem(&za, buf.data(), buf.size(), 0)) return false;
    mz_uint cnt = mz_zip_reader_get_num_files(&za);
    out.clear();
    out.reserve(cnt);
    for (mz_uint i = 0; i < cnt; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) { mz_zip_reader_end(&za); return false; }
        out.push_back({ st.m_filename, mz_zip_reader_is_file_a_directory(&za, i) != 0,
                        (uint64_t)st.m_uncomp_size, (uint64_t)st.m_comp_size, (uint32_t)i });
    }
    mz_zip_reader_end(&za);
    return true;
}

struct ZipBlob { std::vector<uint8_t> deflated; std::vector<uint8_t> raw; uint32_t crc = 0; uint64_t orig = 0; bool store = false; };

inline size_t build_zip(const std::vector<Item>& items, std::vector<uint8_t>& out,
                        int level, bool compress_all, int nthreads, const Progress& progress = {},
                        const std::atomic<bool>* cancel = nullptr) {
    if (level < 1) level = 1;
    if (level > 9) level = 9;

    std::vector<ZipBlob> blobs(items.size());
    std::atomic<size_t> done{0};
    mz_uint dflags = tdefl_create_comp_flags_from_zip_params(level, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);

    parallel_for(items.size(), nthreads, [&](size_t i) {
        const Item& it = items[i];
        ZipBlob& b = blobs[i];
        if (!it.is_dir) {
            auto data = read_file(it.abs, (size_t)it.size, 0);
            b.orig = it.size;
            b.crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, data.data(), data.size());
            bool store = (!compress_all && is_precompressed(it.name)) || data.empty();
            if (!store) {
                size_t clen = 0;
                void* c = tdefl_compress_mem_to_heap(data.data(), data.size(), &clen, (int)dflags);
                if (c && clen < data.size()) b.deflated.assign((uint8_t*)c, (uint8_t*)c + clen);
                else store = true;
                if (c) mz_free(c);
            }
            b.store = store;
            if (store) b.raw = std::move(data);
        }
        if (progress) progress(done.fetch_add(1) + 1, items.size());
    }, cancel);
    if (cancel && cancel->load()) { out.clear(); return 0; }

    mz_zip_archive za;
    memset(&za, 0, sizeof(za));
    if (!mz_zip_writer_init_heap(&za, 0, 1 << 20)) return 0;
    bool ok = true;
    for (size_t i = 0; i < items.size() && ok; ++i) {
        const Item& it = items[i];
        ZipBlob& b = blobs[i];
        if (it.is_dir) {
            std::string d = it.name + "/";
            ok = mz_zip_writer_add_mem(&za, d.c_str(), nullptr, 0, 0) != 0;
        } else if (b.store) {
            ok = mz_zip_writer_add_mem(&za, it.name.c_str(), b.raw.data(), b.raw.size(), 0) != 0;
        } else {
            ok = mz_zip_writer_add_mem_ex(&za, it.name.c_str(), b.deflated.data(), b.deflated.size(),
                                          nullptr, 0, (mz_uint)level | MZ_ZIP_FLAG_COMPRESSED_DATA,
                                          b.orig, b.crc) != 0;
        }
        b.deflated.clear(); b.deflated.shrink_to_fit();
        b.raw.clear(); b.raw.shrink_to_fit();
    }
    void* buf = nullptr;
    size_t n = 0;
    if (ok) ok = mz_zip_writer_finalize_heap_archive(&za, &buf, &n) != 0;
    mz_zip_writer_end(&za);
    if (!ok) { if (buf) mz_free(buf); return 0; }
    out.assign((uint8_t*)buf, (uint8_t*)buf + n);
    mz_free(buf);
    return out.size();
}

inline int zip_extract(const fs::path& p, const fs::path& dest, const std::vector<int>* sel,
                       int nthreads = 0, const Progress& progress = {}, int* failures = nullptr,
                       const std::atomic<bool>* cancel = nullptr,
                       std::vector<std::string>* failed_names = nullptr) {
    std::error_code ec0;
    auto n = fs::file_size(p, ec0);
    if (ec0) { if (failures) *failures = 1; return 0; }
    auto buf = read_file(p, (size_t)n, 0);

    std::vector<uint32_t> idx;
    {
        mz_zip_archive za;
        memset(&za, 0, sizeof(za));
        if (!mz_zip_reader_init_mem(&za, buf.data(), buf.size(), 0)) { if (failures) *failures = 1; return 0; }
        if (sel) { for (int s : *sel) idx.push_back((uint32_t)s); }
        else     { mz_uint c = mz_zip_reader_get_num_files(&za); for (mz_uint i = 0; i < c; ++i) idx.push_back(i); }
        mz_zip_reader_end(&za);
    }

    std::atomic<int> files{0}, bad{0};
    std::atomic<size_t> done{0};
    std::mutex fmtx;
    const void* key = buf.data();
    size_t klen = buf.size();

    parallel_for(idx.size(), nthreads, [&](size_t k) {
        std::string fname = "entry #" + std::to_string(idx[k]);
        static thread_local mz_zip_archive tza;
        static thread_local const void* tkey = nullptr;
        if (tkey != key) {
            if (tkey) mz_zip_reader_end(&tza);
            memset(&tza, 0, sizeof(tza));
            tkey = mz_zip_reader_init_mem(&tza, key, klen, 0) ? key : nullptr;
        }
        std::error_code ec;
        bool ok = false;
        if (tkey) {
            uint32_t i = idx[k];
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&tza, i, &st)) {
                fname = st.m_filename;
                fs::path rel = fs::path(st.m_filename).lexically_normal();
                if (safe_rel(rel)) {
                    fs::path dst = dest / rel;
                    if (mz_zip_reader_is_file_a_directory(&tza, i)) {
                        fs::create_directories(dst, ec);
                        ok = true;
                    } else {
                        fs::create_directories(dst.parent_path(), ec);
                        size_t usz = (size_t)st.m_uncomp_size;
                        std::vector<uint8_t> data(usz);
                        bool got = usz == 0 || mz_zip_reader_extract_to_mem(&tza, i, data.data(), usz, 0) != 0;
                        if (got && write_file(dst, data.data(), usz)) { files.fetch_add(1); ok = true; }
                    }
                }
            }
        }
        if (!ok) {
            bad.fetch_add(1);
            if (failed_names) { std::lock_guard<std::mutex> lk(fmtx); failed_names->push_back(fname); }
        }
        if (progress) progress(done.fetch_add(1) + 1, idx.size());
    }, cancel);

    if (failures) *failures = bad.load();
    return files.load();
}

}
#endif

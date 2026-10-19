#pragma once

#include "satzip_ops.hpp"

#if defined(SATZIP_HAVE_7Z)
#include "7z.h"
#include "7zAlloc.h"
#include "7zBuf.h"
#include "7zCrc.h"

namespace sz {

struct SevenZEntry { std::string name; bool is_dir; uint64_t orig; uint32_t idx; };

inline bool is_7z_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    unsigned char m[6] = {0};
    f.read((char*)m, 6);
    return f.gcount() == 6 && m[0] == 0x37 && m[1] == 0x7A && m[2] == 0xBC &&
           m[3] == 0xAF && m[4] == 0x27 && m[5] == 0x1C;
}

namespace sevenz {

inline ISzAlloc& alloc() { static ISzAlloc a = { SzAlloc, SzFree }; return a; }

struct EncProgress { ICompressProgress vt; const Progress* progress; const std::atomic<bool>* cancel; uint64_t total; };

inline SRes enc_progress(const ICompressProgress* pp, UInt64 in_size, UInt64) {
    const EncProgress* e = (const EncProgress*)pp;
    if (e->cancel && *e->cancel) return SZ_ERROR_PROGRESS;
    if (*e->progress && in_size != (UInt64)(Int64)-1) (*e->progress)((size_t)in_size, (size_t)e->total);
    return SZ_OK;
}

struct MemStream { ISeekInStream vt; const uint8_t* data; size_t size; size_t pos; };

inline SRes mem_read(const ISeekInStream* pp, void* buf, size_t* size) {
    MemStream* s = (MemStream*)pp;
    size_t n = *size;
    if (n > s->size - s->pos) n = s->size - s->pos;
    if (n) memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}
inline SRes mem_seek(const ISeekInStream* pp, Int64* pos, ESzSeek origin) {
    MemStream* s = (MemStream*)pp;
    Int64 np = *pos;
    if (origin == SZ_SEEK_CUR) np += (Int64)s->pos;
    else if (origin == SZ_SEEK_END) np += (Int64)s->size;
    if (np < 0 || (UInt64)np > s->size) return SZ_ERROR_PARAM;
    s->pos = (size_t)np;
    *pos = np;
    return SZ_OK;
}

struct Reader {
    std::vector<uint8_t> buf;
    std::vector<Byte> lookbuf;
    MemStream ms{};
    CLookToRead2 look{};
    CSzArEx db{};
    bool ok = false;

    bool open(const fs::path& p) {
        static bool crc = false;
        if (!crc) { CrcGenerateTable(); crc = true; }
        std::error_code ec;
        auto n = fs::file_size(p, ec);
        if (ec) return false;
        buf = read_file(p, (size_t)n, 0);
        ms.vt.Read = mem_read; ms.vt.Seek = mem_seek;
        ms.data = buf.data(); ms.size = buf.size(); ms.pos = 0;
        LookToRead2_CreateVTable(&look, False);
        lookbuf.resize(1u << 18);
        look.buf = lookbuf.data(); look.bufSize = lookbuf.size();
        look.realStream = &ms.vt;
        LookToRead2_Init(&look);
        SzArEx_Init(&db);
        ok = SzArEx_Open(&db, &look.vt, &alloc(), &alloc()) == SZ_OK;
        return ok;
    }
    std::string name(uint32_t i) {
        size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        std::vector<UInt16> w(len);
        SzArEx_GetFileNameUtf16(&db, i, w.data());
        std::string out;
        for (size_t k = 0; k < len && w[k]; ++k) {
            uint32_t c = w[k];
            if (c >= 0xD800 && c < 0xDC00 && k + 1 < len && w[k + 1] >= 0xDC00 && w[k + 1] < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (w[k + 1] - 0xDC00);
                ++k;
            }
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
            else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
            else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 63)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        }
        return out;
    }
    ~Reader() { if (ok) SzArEx_Free(&db, &alloc()); }
};

}

inline bool sevenz_list(const fs::path& p, std::vector<SevenZEntry>& out) {
    sevenz::Reader r;
    if (!r.open(p)) return false;
    out.clear();
    out.reserve(r.db.NumFiles);
    for (UInt32 i = 0; i < r.db.NumFiles; ++i)
        out.push_back({ r.name(i), SzArEx_IsDir(&r.db, i) != 0,
                        (uint64_t)SzArEx_GetFileSize(&r.db, i), (uint32_t)i });
    return true;
}

inline int sevenz_extract(const fs::path& p, const fs::path& dest, const std::vector<int>* sel,
                          const Progress& progress = {}, int* failures = nullptr,
                          const std::atomic<bool>* cancel = nullptr,
                          std::vector<std::string>* failed_names = nullptr) {
    sevenz::Reader r;
    if (!r.open(p)) { if (failures) *failures = 1; return 0; }

    std::vector<uint32_t> idx;
    if (sel) { for (int s : *sel) idx.push_back((uint32_t)s); }
    else     { for (UInt32 i = 0; i < r.db.NumFiles; ++i) idx.push_back(i); }

    auto mtime_of = [&r](uint32_t i) -> int64_t {
        if (!SzBitWithVals_Check(&r.db.MTime, i)) return SZ_AR_NO_MTIME;
        const CNtfsFileTime& ft = r.db.MTime.Vals[i];
        return ((int64_t)(((uint64_t)ft.High << 32) | ft.Low) - 116444736000000000LL) * 100;
    };
    std::vector<std::pair<fs::path, int64_t>> dir_times;
    UInt32 blockIndex = 0xFFFFFFFF;
    Byte*  outBuffer = NULL;
    size_t outBufferSize = 0;
    int files = 0, bad = 0;
    size_t done = 0;
    std::error_code ec;

    for (uint32_t i : idx) {
        if (cancel && *cancel) break;
        std::string nm = r.name(i);
        fs::path rel = fs::path(nm).lexically_normal();
        bool this_ok = true;
        if (safe_rel(rel)) {
            fs::path dst = dest / rel;
            if (SzArEx_IsDir(&r.db, i)) {
                fs::create_directories(dst, ec);
                dir_times.push_back({ dst, mtime_of(i) });
            } else {
                size_t offset = 0, outSize = 0;
                SRes res = SzArEx_Extract(&r.db, &r.look.vt, i, &blockIndex,
                                          &outBuffer, &outBufferSize, &offset, &outSize,
                                          &sevenz::alloc(), &sevenz::alloc());
                fs::create_directories(dst.parent_path(), ec);
                if (res == SZ_OK && write_file(dst, outBuffer + offset, outSize)) { restore_mtime(dst, mtime_of(i)); ++files; }
                else this_ok = false;
            }
        } else this_ok = false;
        if (!this_ok) {
            ++bad;
            if (failed_names) failed_names->push_back(nm);
        }
        if (progress) progress(++done, idx.size());
    }
    if (outBuffer) sevenz::alloc().Free(&sevenz::alloc(), outBuffer);
    for (auto& d : dir_times) restore_mtime(d.first, d.second);
    if (failures) *failures = bad;
    return files;
}

}
#endif

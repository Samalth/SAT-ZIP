#pragma once

#include "satzip_tar.hpp"
#include "satzip_7z.hpp"

#if defined(SATZIP_HAVE_7Z)
#include "Lzma2Enc.h"
#include "Lzma2Dec.h"
#include "satzip_archive.h"

namespace sz {

namespace xz {

inline void varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) { out.push_back((uint8_t)(v & 0x7F) | 0x80); v >>= 7; }
    out.push_back((uint8_t)v);
}

inline void u32le(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i)));
}

}

inline size_t xz_compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out, int level,
                          const Progress& progress = {}, const std::atomic<bool>* cancel = nullptr) {
    using namespace xz;
    if (level < 1) level = 1;
    if (level > 9) level = 9;

    CLzma2EncHandle enc = Lzma2Enc_Create(&sevenz::alloc(), &sevenz::alloc());
    if (!enc) return 0;
    CLzma2EncProps p;
    Lzma2EncProps_Init(&p);
    p.lzmaProps.level = level;
    p.lzmaProps.reduceSize = n;
    if (Lzma2Enc_SetProps(enc, &p) != SZ_OK) { Lzma2Enc_Destroy(enc); return 0; }
    Byte propsByte = Lzma2Enc_WriteProperties(enc);

    std::vector<uint8_t> packed(n + n / 2 + 4096);
    size_t packedLen = packed.size();
    sevenz::EncProgress ep{ { sevenz::enc_progress }, &progress, cancel, n };
    SRes r = Lzma2Enc_Encode2(enc, nullptr, packed.data(), &packedLen, nullptr, src, n, &ep.vt);
    Lzma2Enc_Destroy(enc);
    if (r != SZ_OK) return 0;
    packed.resize(packedLen);

    std::vector<uint8_t> bh;
    bh.push_back(0);
    bh.push_back(0x00);
    bh.push_back(0x21);
    bh.push_back(0x01);
    bh.push_back(propsByte);
    while ((bh.size() + 4) % 4 != 0) bh.push_back(0x00);
    bh[0] = (uint8_t)((bh.size() + 4) / 4 - 1);
    uint32_t bhcrc = sz_crc32(bh.data(), bh.size());

    uint64_t unpadded = bh.size() + 4 + packedLen + 4;
    size_t blockpad = (4 - (packedLen % 4)) % 4;
    uint32_t datacrc = sz_crc32(src, n);

    std::vector<uint8_t> index;
    index.push_back(0x00);
    varint(index, 1);
    varint(index, unpadded);
    varint(index, (uint64_t)n);
    while (index.size() % 4 != 0) index.push_back(0x00);
    uint32_t idxcrc = sz_crc32(index.data(), index.size());
    uint64_t index_size = index.size() + 4;

    std::vector<uint8_t> ftr;
    u32le(ftr, (uint32_t)(index_size / 4 - 1));
    ftr.push_back(0x00);
    ftr.push_back(0x01);
    uint32_t ftrcrc = sz_crc32(ftr.data(), ftr.size());

    out.clear();
    out.reserve(12 + bh.size() + 4 + packedLen + blockpad + 4 + index_size + 12);
    static const uint8_t magic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
    out.insert(out.end(), magic, magic + 6);
    out.push_back(0x00); out.push_back(0x01);
    {
        uint8_t f[2] = { 0x00, 0x01 };
        u32le(out, sz_crc32(f, 2));
    }
    out.insert(out.end(), bh.begin(), bh.end());
    u32le(out, bhcrc);
    out.insert(out.end(), packed.begin(), packed.end());
    out.insert(out.end(), blockpad, 0x00);
    u32le(out, datacrc);
    out.insert(out.end(), index.begin(), index.end());
    u32le(out, idxcrc);
    u32le(out, ftrcrc);
    out.insert(out.end(), ftr.begin(), ftr.end());
    out.push_back('Y'); out.push_back('Z');
    return out.size();
}

inline size_t build_tar_xz(const std::vector<Item>& items, std::vector<uint8_t>& out,
                           int level, const Progress& progress = {},
                           const std::atomic<bool>* cancel = nullptr) {
    std::vector<uint8_t> tarball;
    if (!build_tar(items, tarball)) return 0;
    if (cancel && *cancel) return 0;
    return xz_compress(tarball.data(), tarball.size(), out, level, progress, cancel);
}

inline bool is_xz_data(const uint8_t* p, size_t n) {
    return n >= 6 && p[0] == 0xFD && p[1] == '7' && p[2] == 'z' && p[3] == 'X' && p[4] == 'Z' && p[5] == 0x00;
}

inline bool is_xz_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    unsigned char m[6] = {0};
    f.read((char*)m, 6);
    return f.gcount() == 6 && is_xz_data(m, 6);
}

namespace xz {

inline bool rd_varint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int i = 0; i < 9 && p < end; ++i) {
        v |= (uint64_t)(*p & 0x7F) << (7 * i);
        if (!(*p++ & 0x80)) return true;
    }
    return false;
}

}

inline bool xz_decompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    using namespace xz;
    if (!is_xz_data(src, n) || n < 32) return false;
    uint8_t check_type = src[7] & 0x0F;
    size_t check_size = check_type == 0 ? 0 : check_type <= 3 ? 4 : check_type <= 6 ? 8 : 32;

    const uint8_t* ftr = src + n - 12;
    if (ftr[10] != 'Y' || ftr[11] != 'Z') return false;
    uint32_t backward = 0;
    for (int i = 0; i < 4; ++i) backward |= (uint32_t)ftr[4 + i] << (8 * i);
    size_t index_size = ((size_t)backward + 1) * 4;
    if (index_size + 24 > n) return false;

    const uint8_t* idx = src + n - 12 - index_size;
    const uint8_t* ip = idx;
    const uint8_t* iend = idx + index_size;
    if (*ip++ != 0x00) return false;
    uint64_t nrec = 0;
    if (!rd_varint(ip, iend, nrec) || nrec == 0 || nrec > 4096) return false;

    uint64_t total = 0;
    std::vector<std::pair<uint64_t, uint64_t>> recs;
    for (uint64_t r = 0; r < nrec; ++r) {
        uint64_t unpadded = 0, usize = 0;
        if (!rd_varint(ip, iend, unpadded) || !rd_varint(ip, iend, usize)) return false;
        recs.push_back({ unpadded, usize });
        total += usize;
    }
    if (total > (1ull << 33)) return false;
    out.resize((size_t)total);

    const uint8_t* bp = src + 12;
    size_t opos = 0;
    for (auto& rec : recs) {
        if ((size_t)(bp - src) + rec.first > n) return false;
        const uint8_t* bh = bp;
        size_t hsize = ((size_t)bh[0] + 1) * 4;
        if (bh[0] == 0 || hsize > rec.first) return false;
        uint8_t bflags = bh[1];
        if ((bflags & 0x03) != 0) return false;
        const uint8_t* fp = bh + 2;
        if (bflags & 0x40) { uint64_t d; if (!rd_varint(fp, bh + hsize, d)) return false; }
        if (bflags & 0x80) { uint64_t d; if (!rd_varint(fp, bh + hsize, d)) return false; }
        uint64_t fid = 0, psz = 0;
        if (!rd_varint(fp, bh + hsize, fid) || fid != 0x21) return false;
        if (!rd_varint(fp, bh + hsize, psz) || psz != 1 || fp >= bh + hsize) return false;
        uint8_t prop = *fp;

        size_t data_len = rec.first - hsize - check_size;
        const uint8_t* data = bp + hsize;

        SizeT dl = (SizeT)(rec.second);
        SizeT sl = (SizeT)data_len;
        ELzmaStatus st;
        SRes r = Lzma2Decode(out.data() + opos, &dl, data, &sl, prop,
                             LZMA_FINISH_END, &st, &sevenz::alloc());
        if (r != SZ_OK || dl != rec.second) return false;

        if (check_type == 1) {
            uint32_t want = 0;
            const uint8_t* chk = bp + hsize + data_len + ((4 - (data_len % 4)) % 4);
            for (int i = 0; i < 4; ++i) want |= (uint32_t)chk[i] << (8 * i);
            if (sz_crc32(out.data() + opos, (size_t)rec.second) != want) return false;
        }
        opos += (size_t)rec.second;
        size_t padded = rec.first + ((4 - (rec.first % 4)) % 4);
        bp += padded;
    }
    return opos == out.size();
}

}
#endif

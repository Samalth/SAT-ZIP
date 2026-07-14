#pragma once

#include "satzip_7z.hpp"

#if defined(SATZIP_HAVE_7Z)
#include "LzmaEnc.h"

namespace sz {

namespace sevenz {

inline void wr_num(std::vector<uint8_t>& out, uint64_t value) {
    uint8_t first = 0, mask = 0x80;
    int i;
    for (i = 0; i < 8; i++) {
        if (value < ((uint64_t)1 << (7 * (i + 1)))) { first |= (uint8_t)(value >> (8 * i)); break; }
        first |= mask; mask >>= 1;
    }
    out.push_back(first);
    for (; i > 0; i--) { out.push_back((uint8_t)value); value >>= 8; }
}

inline void wr_bits(std::vector<uint8_t>& out, const std::vector<char>& bits) {
    uint8_t b = 0, mask = 0x80;
    for (char bit : bits) {
        if (bit) b |= mask;
        mask >>= 1;
        if (!mask) { out.push_back(b); b = 0; mask = 0x80; }
    }
    if (mask != 0x80) out.push_back(b);
}

inline void wr_u32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); }
inline void wr_u64(std::vector<uint8_t>& out, uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back((uint8_t)(v >> (8 * i))); }

inline void wr_name(std::vector<uint8_t>& out, const std::string& s) {
    size_t i = 0;
    auto emit16 = [&](uint16_t w) { out.push_back((uint8_t)w); out.push_back((uint8_t)(w >> 8)); };
    while (i < s.size()) {
        uint32_t c = (uint8_t)s[i]; int extra = 0;
        if (c < 0x80) extra = 0;
        else if ((c >> 5) == 0x6)  { c &= 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE)  { c &= 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { c &= 0x07; extra = 3; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) c = (c << 6) | ((uint8_t)s[i] & 0x3F);
        if (c >= 0x10000) { c -= 0x10000; emit16((uint16_t)(0xD800 + (c >> 10))); emit16((uint16_t)(0xDC00 + (c & 0x3FF))); }
        else emit16((uint16_t)c);
    }
    emit16(0);
}

}

inline size_t sevenz_write(const std::vector<Item>& items, std::vector<uint8_t>& out,
                           int level, const Progress& progress = {}) {
    using namespace sevenz;
    if (level < 1) level = 1; if (level > 9) level = 9;

    std::vector<uint8_t> raw;
    std::vector<uint64_t> csizes;
    std::vector<uint32_t> ccrc;
    std::vector<char> empty_stream, empty_file;
    bool any_empty = false, any_empty_file = false;

    size_t done = 0;
    for (const auto& it : items) {
        if (it.is_dir)        { empty_stream.push_back(1); empty_file.push_back(0); any_empty = true; }
        else if (it.size == 0) { empty_stream.push_back(1); empty_file.push_back(1); any_empty = true; any_empty_file = true; }
        else {
            empty_stream.push_back(0); empty_file.push_back(0);
            auto data = read_file(it.abs, (size_t)it.size, 0);
            ccrc.push_back(sz_crc32(data.data(), data.size()));
            csizes.push_back(data.size());
            raw.insert(raw.end(), data.begin(), data.end());
        }
        if (progress) progress(++done, items.size());
    }

    std::vector<uint8_t> packed;
    uint8_t propsEnc[5];
    bool have_content = !raw.empty();
    if (have_content) {
        CLzmaEncProps p; LzmaEncProps_Init(&p);
        p.level = level; p.reduceSize = raw.size();
        packed.resize(raw.size() + raw.size() / 2 + 4096);
        SizeT destLen = packed.size(), ps = 5;
        SRes r = LzmaEncode(packed.data(), &destLen, raw.data(), raw.size(), &p,
                            propsEnc, &ps, 0, nullptr, &sevenz::alloc(), &sevenz::alloc());
        if (r != SZ_OK || ps != 5) return 0;
        packed.resize(destLen);
    }

    std::vector<uint8_t> h;
    h.push_back(0x01);
    if (have_content) {
        h.push_back(0x04);
        h.push_back(0x06); wr_num(h, 0); wr_num(h, 1);
        h.push_back(0x09); wr_num(h, packed.size()); h.push_back(0x00);
        h.push_back(0x07);
        h.push_back(0x0B); wr_num(h, 1); h.push_back(0x00);
        wr_num(h, 1);
        h.push_back(0x23);
        h.push_back(0x03); h.push_back(0x01); h.push_back(0x01);
        wr_num(h, 5); h.insert(h.end(), propsEnc, propsEnc + 5);
        h.push_back(0x0C); wr_num(h, raw.size());
        h.push_back(0x00);
        h.push_back(0x08);
        h.push_back(0x0D); wr_num(h, csizes.size());
        if (csizes.size() > 1) { h.push_back(0x09); for (size_t i = 0; i + 1 < csizes.size(); ++i) wr_num(h, csizes[i]); }
        h.push_back(0x0A); h.push_back(0x01);
        for (uint32_t c : ccrc) wr_u32(h, c);
        h.push_back(0x00);
        h.push_back(0x00);
    }

    h.push_back(0x05);
    wr_num(h, items.size());
    if (any_empty) {
        std::vector<uint8_t> bv; wr_bits(bv, empty_stream);
        h.push_back(0x0E); wr_num(h, bv.size()); h.insert(h.end(), bv.begin(), bv.end());
    }
    if (any_empty_file) {
        std::vector<char> ef;
        for (size_t i = 0; i < items.size(); ++i) if (empty_stream[i]) ef.push_back(empty_file[i]);
        std::vector<uint8_t> bv; wr_bits(bv, ef);
        h.push_back(0x0F); wr_num(h, bv.size()); h.insert(h.end(), bv.begin(), bv.end());
    }
    {
        std::vector<uint8_t> names; names.push_back(0);
        for (const auto& it : items) wr_name(names, it.name);
        h.push_back(0x11); wr_num(h, names.size()); h.insert(h.end(), names.begin(), names.end());
    }
    h.push_back(0x00);
    h.push_back(0x00);

    uint64_t nextOff = packed.size();
    uint64_t nextSize = h.size();
    uint32_t nextCrc = sz_crc32(h.data(), h.size());

    std::vector<uint8_t> start;
    wr_u64(start, nextOff); wr_u64(start, nextSize); wr_u32(start, nextCrc);
    uint32_t startCrc = sz_crc32(start.data(), start.size());

    out.clear();
    out.reserve(32 + packed.size() + h.size());
    static const uint8_t sig[6] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
    out.insert(out.end(), sig, sig + 6);
    out.push_back(0x00); out.push_back(0x04);
    wr_u32(out, startCrc);
    out.insert(out.end(), start.begin(), start.end());
    out.insert(out.end(), packed.begin(), packed.end());
    out.insert(out.end(), h.begin(), h.end());
    return out.size();
}

}
#endif

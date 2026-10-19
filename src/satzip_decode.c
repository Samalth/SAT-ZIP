#include "satzip.h"

SZ_INLINE void sz_copy_match8(uint8_t* op, const uint8_t* m, uint8_t* m_end) {
    do { memcpy(op, m, 8); op += 8; m += 8; } while (op < m_end);
}

SZ_INLINE void sz_copy_match_small(uint8_t* op, const uint8_t* m, uint8_t* m_end, uint32_t off) {
    static const unsigned inc32[8] = {0, 1, 2, 1, 0, 4, 4, 4};
    static const int      dec64[8] = {0, 0, 0, -1, -4, 1, 2, 3};
    op[0] = m[0]; op[1] = m[1]; op[2] = m[2]; op[3] = m[3];
    m += inc32[off];
    memcpy(op + 4, m, 4);
    m -= dec64[off];
    op += 8;
    while (op < m_end) { memcpy(op, m, 8); op += 8; m += 8; }
}

SZ_INLINE size_t sz_read_ext(const uint8_t** ipp, const uint8_t* iend, size_t base) {
    const uint8_t* ip = *ipp;
    uint8_t b;
    do {
        if (SZ_UNLIKELY(ip >= iend)) { *ipp = ip; return SZ_ERR; }
        b = *ip++;
        base += b;
    } while (b == 255);
    *ipp = ip;
    return base;
}

size_t sz_decompress_block(const uint8_t* src, size_t src_len,
                           uint8_t* dst, size_t dst_capacity) {
    if (SZ_UNLIKELY(src_len == 0)) return 0;

    const uint8_t*       ip     = src;
    const uint8_t* const iend   = src + src_len;
    uint8_t*             op     = dst;
    uint8_t* const       oend   = dst + dst_capacity;
    const uint8_t* const ilimit = iend - 32;
    uint8_t* const       olimit = oend - 32;

    for (;;) {
        const uint32_t token  = *ip++;
        size_t         litlen = token >> 4;

        if (SZ_LIKELY(litlen < 15 && ip + 16 <= ilimit && op + 16 <= olimit)) {
            memcpy(op, ip, 16);
            op += litlen;
            ip += litlen;
        } else {
            if (SZ_UNLIKELY(litlen == 15)) {
                litlen = sz_read_ext(&ip, iend, 15);
                if (SZ_UNLIKELY(litlen == SZ_ERR)) return SZ_ERR;
            }
            uint8_t* const lit_end = op + litlen;
            if (SZ_LIKELY(lit_end <= olimit && ip + litlen <= ilimit)) {
                sz_wildcopy(op, ip, lit_end);
            } else {
                if (SZ_UNLIKELY(lit_end > oend || ip + litlen > iend)) return SZ_ERR;
                memcpy(op, ip, litlen);
            }
            op  = lit_end;
            ip += litlen;
        }

        if (SZ_UNLIKELY(ip >= iend)) break;

        uint32_t off = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8);
        ip += 2;
        const uint8_t* match = op - off;
        if (SZ_UNLIKELY(off == 0 || match < dst)) return SZ_ERR;

        size_t mcode = token & 15;
        if (SZ_LIKELY(mcode < 15 && off >= 8 && op + 24 <= olimit)) {
            memcpy(op, match, 8);
            memcpy(op + 8, match + 8, 8);
            memcpy(op + 16, match + 16, 8);
            op += mcode + SZ_MIN_MATCH;
            continue;
        }
        if (SZ_UNLIKELY(mcode == 15)) {
            mcode = sz_read_ext(&ip, iend, 15);
            if (SZ_UNLIKELY(mcode == SZ_ERR)) return SZ_ERR;
        }
        const size_t   mlen  = mcode + SZ_MIN_MATCH;
        uint8_t* const m_end = op + mlen;

        if (SZ_UNLIKELY(m_end > oend)) return SZ_ERR;
        if (SZ_LIKELY(off >= 32))  sz_wildcopy(op, match, m_end);
        else if (SZ_LIKELY(off >= 8)) sz_copy_match8(op, match, m_end);
        else                       sz_copy_match_small(op, match, m_end, off);
        op = m_end;
    }

    return (size_t)(op - dst);
}

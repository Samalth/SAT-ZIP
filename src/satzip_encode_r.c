#include "satzip_emit.h"

SZ_INLINE size_t sz_find_r(const uint8_t* in, const uint8_t* p, sz_state_t* st,
                           const uint8_t* mlimit, uint32_t* off_out, int depth) {
    uint32_t pos = (uint32_t)(p - in);
    uint32_t h   = sz_hash8(sz_load64(p));
    uint32_t ref = st->table[h];
    st->chain[pos] = (uint16_t)ref;
    st->table[h]   = pos;

    size_t   best = 0;
    uint32_t boff = 0;
    uint32_t cur32 = sz_load32(p);
    while (ref < pos && depth-- > 0) {
        const uint8_t* m = in + ref;
        if (sz_load32(m) == cur32) {
            size_t l = sz_match_len(p, m, mlimit);
            if (l > best) { best = l; boff = pos - ref; }
        }
        uint32_t nxt = st->chain[ref];
        if (nxt >= ref) break;
        ref = nxt;
    }
    if (best >= SZ_MIN_MATCH) { *off_out = boff; return best; }
    return 0;
}

size_t sz_compress_block_r(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                           int acceleration) {
    if (SZ_UNLIKELY(in_len > SZ_BLOCK_SIZE)) return 0;
    if (SZ_UNLIKELY(in_len < SZ_MIN_MATCH + SZ_LAST_LITS)) return 0;
    unsigned accel = acceleration < 1 ? 1u : acceleration > 9 ? 9u : (unsigned)acceleration;
    int depth = 128 >> accel;
    if (depth < 4) depth = 4;

    const uint8_t*       ip     = in;
    const uint8_t*       anchor = in;
    const uint8_t* const iend   = in + in_len;
    const uint8_t* const mlimit = iend - SZ_LAST_LITS;
    uint8_t*             op     = out;

    unsigned attempts = 1u << 6;
    while (ip < mlimit) {
        uint32_t off;
        size_t mlen = sz_find_r(in, ip, st, mlimit, &off, depth);
        if (mlen < SZ_MIN_MATCH) { ip += (attempts++ >> 6); continue; }
        attempts = 1u << 6;

        while (ip + 1 < mlimit) {
            uint32_t off2;
            size_t mlen2 = sz_find_r(in, ip + 1, st, mlimit, &off2, depth);
            if (mlen2 > mlen) { ++ip; off = off2; mlen = mlen2; }
            else break;
        }

        op = sz_emit_token(op, anchor, (size_t)(ip - anchor), off, mlen - SZ_MIN_MATCH);

        const uint8_t* mend = ip + mlen;
        for (const uint8_t* q = ip + 1; q < mend; ++q) {
            uint32_t qp = (uint32_t)(q - in);
            uint32_t qh = sz_hash8(sz_load64(q));
            st->chain[qp] = (uint16_t)st->table[qh];
            st->table[qh] = qp;
        }

        ip     = mend;
        anchor = ip;
    }

    op = sz_emit_last_literals(op, anchor, (size_t)(iend - anchor));
    size_t outlen = (size_t)(op - out);
    return (outlen < in_len) ? outlen : 0;
}

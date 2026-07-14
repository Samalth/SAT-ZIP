#include "satzip_emit.h"

#define SZ_SKIP_TRIGGER 6

size_t sz_compress_block(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                         int acceleration) {
    if (SZ_UNLIKELY(in_len > SZ_BLOCK_SIZE)) return 0;
    if (SZ_UNLIKELY(in_len < SZ_MIN_MATCH + SZ_LAST_LITS)) return 0;
    unsigned accel = acceleration < 1 ? 1u : (unsigned)acceleration;

    const uint8_t*       ip     = in;
    const uint8_t*       anchor = in;
    const uint8_t* const iend   = in + in_len;
    const uint8_t* const mlimit = iend - SZ_LAST_LITS;
    uint8_t*             op     = out;
    uint32_t* const      table  = st->table;

    for (;;) {
        const uint8_t* match;
        uint32_t       off;

        {
            const uint8_t* forward = ip;
            unsigned step    = 1;
            unsigned attempts = accel << SZ_SKIP_TRIGGER;
            for (;;) {
                ip = forward;
                if (SZ_UNLIKELY(ip >= mlimit)) goto finish;
                uint32_t pos = (uint32_t)(ip - in);
                uint32_t h   = sz_hash8(sz_load64(ip));
                uint32_t ref = table[h];
                table[h]     = pos;
                forward      = ip + step;
                step         = (attempts++ >> SZ_SKIP_TRIGGER);
                if (ref < pos) {
                    off   = pos - ref;
                    match = in + ref;
                    if (off <= SZ_MAX_OFFSET && sz_load32(match) == sz_load32(ip)) break;
                }
            }
        }

        size_t mlen = sz_match_len(ip, match, mlimit);
        if (SZ_UNLIKELY(mlen < SZ_MIN_MATCH)) goto finish;

        op = sz_emit_token(op, anchor, (size_t)(ip - anchor), off, mlen - SZ_MIN_MATCH);
        ip    += mlen;
        anchor = ip;

        if (SZ_LIKELY(ip < mlimit)) {
            uint32_t p2 = (uint32_t)(ip - 2 - in);
            table[sz_hash8(sz_load64(in + p2))] = p2;
        }
    }

finish:
    op = sz_emit_last_literals(op, anchor, (size_t)(iend - anchor));
    size_t outlen = (size_t)(op - out);
    return (outlen < in_len) ? outlen : 0;
}

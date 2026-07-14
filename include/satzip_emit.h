#ifndef SATZIP_EMIT_H
#define SATZIP_EMIT_H

#include "satzip.h"

SZ_INLINE uint8_t* sz_emit_len(uint8_t* op, size_t len) {
    if (SZ_LIKELY(len < 255)) { *op++ = (uint8_t)len; return op; }
    while (len >= 255) { *op++ = 255; len -= 255; }
    *op++ = (uint8_t)len;
    return op;
}

SZ_INLINE uint8_t* sz_emit_token(uint8_t* op, const uint8_t* anchor, size_t litlen,
                                 uint32_t off, size_t mcode) {
    uint8_t* token = op++;
    *token = (uint8_t)(((litlen < 15 ? litlen : 15) << 4) | (mcode < 15 ? mcode : 15));
    if (SZ_UNLIKELY(litlen >= 15)) op = sz_emit_len(op, litlen - 15);
    sz_wildcopy(op, anchor, op + litlen);
    op += litlen;
    op[0] = (uint8_t)(off & 0xFF);
    op[1] = (uint8_t)(off >> 8);
    op += 2;
    if (SZ_UNLIKELY(mcode >= 15)) op = sz_emit_len(op, mcode - 15);
    return op;
}

SZ_INLINE uint8_t* sz_emit_last_literals(uint8_t* op, const uint8_t* anchor, size_t litlen) {
    uint8_t* token = op++;
    *token = (uint8_t)((litlen < 15 ? litlen : 15) << 4);
    if (litlen >= 15) op = sz_emit_len(op, litlen - 15);
    memcpy(op, anchor, litlen);
    return op + litlen;
}

#endif

#include "satzip_tans.h"
#include <stdlib.h>

#define SZ_SPLIT_STREAMS 5

typedef struct { uint8_t* p; size_t n; } sbuf;

static void wr32s(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t rd32s(const uint8_t* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i); return v; }

size_t sz_split_compress(const uint8_t* tokens, size_t tlen, uint8_t* dst, size_t cap) {
    if (!tlen || tlen > 0xFFFFFFFFu) return 0;

    uint8_t* mem = (uint8_t*)malloc(tlen * SZ_SPLIT_STREAMS);
    if (!mem) return 0;
    sbuf st[SZ_SPLIT_STREAMS];
    for (int i = 0; i < SZ_SPLIT_STREAMS; ++i) { st[i].p = mem + (size_t)i * tlen; st[i].n = 0; }
    sbuf* S_tok = &st[0]; sbuf* S_lit = &st[1]; sbuf* S_ext = &st[2];
    sbuf* S_olo = &st[3]; sbuf* S_ohi = &st[4];

    const uint8_t* ip   = tokens;
    const uint8_t* iend = tokens + tlen;
    int ok = 1;
    while (ip < iend) {
        uint8_t t = *ip++;
        S_tok->p[S_tok->n++] = t;
        size_t lit = t >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (ip >= iend) { ok = 0; break; }
                b = *ip++;
                S_ext->p[S_ext->n++] = b;
                lit += b;
            } while (b == 255);
            if (!ok) break;
        }
        if (ip + lit > iend) { ok = 0; break; }
        memcpy(S_lit->p + S_lit->n, ip, lit);
        S_lit->n += lit;
        ip += lit;
        if (ip >= iend) break;
        if (ip + 2 > iend) { ok = 0; break; }
        S_olo->p[S_olo->n++] = *ip++;
        S_ohi->p[S_ohi->n++] = *ip++;
        size_t m = t & 15;
        if (m == 15) {
            uint8_t b;
            do {
                if (ip >= iend) { ok = 0; break; }
                b = *ip++;
                S_ext->p[S_ext->n++] = b;
            } while (b == 255);
            if (!ok) break;
        }
    }

    size_t total = 0;
    if (ok) {
        uint8_t* op = dst;
        if (cap < 4) ok = 0;
        else {
            wr32s(op, (uint32_t)tlen);
            op += 4;
            for (int i = 0; i < SZ_SPLIT_STREAMS && ok; ++i) {
                if ((size_t)(op - dst) + 8 > cap) { ok = 0; break; }
                uint8_t* rec = op;
                op += 8;
                size_t room = cap - (size_t)(op - dst);
                size_t c = st[i].n ? sz_ans_pack(st[i].p, st[i].n, op, room) : 0;
                if (!c) {
                    if (st[i].n > room) { ok = 0; break; }
                    memcpy(op, st[i].p, st[i].n);
                    c = st[i].n;
                }
                wr32s(rec, (uint32_t)st[i].n);
                wr32s(rec + 4, (uint32_t)c);
                op += c;
            }
            total = (size_t)(op - dst);
        }
    }
    free(mem);
    if (!ok || total >= tlen) return 0;
    return total;
}

size_t sz_split_decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (n < 4 + 8 * SZ_SPLIT_STREAMS) return SZ_ERR;
    size_t tlen = rd32s(src);
    if (tlen > cap) return SZ_ERR;

    const uint8_t* ip   = src + 4;
    const uint8_t* iend = src + n;
    sbuf st[SZ_SPLIT_STREAMS];
    uint8_t* mem = NULL;
    size_t memcap = 0;
    for (int i = 0; i < SZ_SPLIT_STREAMS; ++i) memcap += tlen;
    mem = (uint8_t*)malloc(memcap ? memcap : 1);
    if (!mem) return SZ_ERR;

    size_t moff = 0;
    for (int i = 0; i < SZ_SPLIT_STREAMS; ++i) {
        if (ip + 8 > iend) { free(mem); return SZ_ERR; }
        uint32_t raw = rd32s(ip);
        uint32_t c   = rd32s(ip + 4);
        ip += 8;
        if (raw > tlen || ip + c > iend) { free(mem); return SZ_ERR; }
        st[i].p = mem + moff;
        st[i].n = raw;
        moff += tlen;
        if (c == raw) {
            memcpy(st[i].p, ip, raw);
        } else {
            if (sz_ans_unpack(ip, c, st[i].p, raw) != raw) { free(mem); return SZ_ERR; }
        }
        ip += c;
    }
    if (ip != iend) { free(mem); return SZ_ERR; }

    sbuf* S_tok = &st[0]; sbuf* S_lit = &st[1]; sbuf* S_ext = &st[2];
    sbuf* S_olo = &st[3]; sbuf* S_ohi = &st[4];
    size_t itok = 0, ilit = 0, iext = 0, iolo = 0, iohi = 0;
    uint8_t* op   = dst;
    uint8_t* oend = dst + tlen;

    while (itok < S_tok->n) {
        uint8_t t = S_tok->p[itok++];
        if (op >= oend) { free(mem); return SZ_ERR; }
        *op++ = t;
        size_t lit = t >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (iext >= S_ext->n || op >= oend) { free(mem); return SZ_ERR; }
                b = S_ext->p[iext++];
                *op++ = b;
                lit += b;
            } while (b == 255);
        }
        if (ilit + lit > S_lit->n || op + lit > oend) { free(mem); return SZ_ERR; }
        memcpy(op, S_lit->p + ilit, lit);
        ilit += lit;
        op += lit;
        if (itok >= S_tok->n) break;
        if (iolo >= S_olo->n || iohi >= S_ohi->n || op + 2 > oend) { free(mem); return SZ_ERR; }
        *op++ = S_olo->p[iolo++];
        *op++ = S_ohi->p[iohi++];
        size_t m = t & 15;
        if (m == 15) {
            uint8_t b;
            do {
                if (iext >= S_ext->n || op >= oend) { free(mem); return SZ_ERR; }
                b = S_ext->p[iext++];
                *op++ = b;
            } while (b == 255);
        }
    }

    int done = (op == oend) && (ilit == S_lit->n) && (iext == S_ext->n) &&
               (iolo == S_olo->n) && (iohi == S_ohi->n);
    free(mem);
    return done ? tlen : SZ_ERR;
}

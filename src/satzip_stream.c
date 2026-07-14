#include "satzip_stream.h"
#include "satzip_tans.h"
#include <stdlib.h>

static void wr32(uint8_t* p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void wr64(uint8_t* p, uint64_t v) { for (int i=0;i<8;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t rd64(const uint8_t* p) { uint64_t v=0; for (int i=0;i<8;++i) v |= (uint64_t)p[i]<<(8*i); return v; }

size_t sz_stream_bound(size_t n, int block_log) {
    if (block_log < SZ_MIN_BLOCK_LOG || block_log > SZ_BLOCK_LOG) return 0;
    size_t bsize = (size_t)1 << block_log;
    size_t nblk  = (n + bsize - 1) / bsize;
    return SZ_FRAME_HDR + nblk * (SZ_BLOCK_REC + sz_compress_bound(bsize)) + SZ_MARGIN;
}

size_t sz_stream_compress(sz_state_t* st, const uint8_t* in, size_t n,
                          uint8_t* out, size_t out_cap, int block_log, int acceleration, int mode) {
    if (block_log < SZ_MIN_BLOCK_LOG || block_log > SZ_BLOCK_LOG) return 0;
    if (out_cap < sz_stream_bound(n, block_log)) return 0;
    size_t bsize = (size_t)1 << block_log;

    uint8_t* scratch = NULL;
    if (mode == SZ_MODE_T) {
        scratch = (uint8_t*)malloc(sz_compress_bound(bsize) + SZ_MARGIN);
        if (!scratch) mode = SZ_MODE_R;
    }

    uint8_t* op = out;
    op[0]='S'; op[1]='A'; op[2]='T'; op[3]='Z';
    op[4]=SZ_STREAM_VERSION; op[5]=(uint8_t)block_log; op[6]=0; op[7]=0;
    wr64(op + 8, (uint64_t)n);
    op += SZ_FRAME_HDR;

    size_t off = 0;
    while (off < n) {
        size_t bn = (n - off < bsize) ? (n - off) : bsize;
        sz_state_reset(st);

        uint8_t* rec     = op;
        uint8_t* payload = op + SZ_BLOCK_REC;
        uint8_t  method;
        size_t   stored;

        if (mode == SZ_MODE_T) {
            size_t t = sz_compress_block_r(st, in + off, bn, scratch, acceleration);
            if (t == 0) {
                method = 0; memcpy(payload, in + off, bn); stored = bn;
            } else {
                size_t a = sz_split_compress(scratch, t, payload, sz_compress_bound(bsize));
                if (a && a < t) { method = 3; stored = a; }
                else            { method = 1; memcpy(payload, scratch, t); stored = t; }
            }
        } else {
            size_t c = (mode == SZ_MODE_R)
                       ? sz_compress_block_r(st, in + off, bn, payload, acceleration)
                       : sz_compress_block(st, in + off, bn, payload, acceleration);
            if (c == 0) { method = 0; memcpy(payload, in + off, bn); stored = bn; }
            else        { method = 1; stored = c; }
        }

        rec[0] = method;
        wr32(rec + 1, (uint32_t)stored);
        wr32(rec + 5, (uint32_t)bn);
        op  = payload + stored;
        off += bn;
    }
    free(scratch);
    return (size_t)(op - out);
}

int sz_stream_header(const uint8_t* in, size_t n, uint64_t* total, int* block_log) {
    if (n < SZ_FRAME_HDR) return -1;
    if (!(in[0]=='S' && in[1]=='A' && in[2]=='T' && in[3]=='Z')) return -1;
    if (in[4] != SZ_STREAM_VERSION) return -1;
    int bl = in[5];
    if (bl < SZ_MIN_BLOCK_LOG || bl > SZ_BLOCK_LOG) return -1;
    if (block_log) *block_log = bl;
    if (total)     *total     = rd64(in + 8);
    return 0;
}

size_t sz_stream_decompress(const uint8_t* in, size_t n, uint8_t* out, size_t out_cap) {
    uint64_t total; int block_log;
    if (sz_stream_header(in, n, &total, &block_log) != 0) return SZ_ERR;
    if (total > out_cap) return SZ_ERR;
    size_t bsize = (size_t)1 << block_log;

    const uint8_t* ip   = in + SZ_FRAME_HDR;
    const uint8_t* iend = in + n;
    uint8_t*       op   = out;
    uint8_t* const oend = out + total;
    uint8_t*       scratch = NULL;
    size_t         scap = sz_compress_bound(bsize) + SZ_MARGIN;

    while (op < oend) {
        if (ip + SZ_BLOCK_REC > iend) goto fail;
        uint8_t  method = ip[0];
        uint32_t stored = rd32(ip + 1);
        uint32_t orig   = rd32(ip + 5);
        ip += SZ_BLOCK_REC;

        if (orig == 0 || orig > bsize) goto fail;
        if (ip + stored > iend)        goto fail;
        if (op + orig  > oend)         goto fail;

        if (method == 0) {
            if (stored != orig) goto fail;
            memcpy(op, ip, orig);
        } else if (method == 1) {
            size_t d = sz_decompress_block(ip, stored, op, orig);
            if (d != orig) goto fail;
        } else if (method == 2 || method == 3) {
            if (!scratch) { scratch = (uint8_t*)malloc(scap); if (!scratch) goto fail; }
            size_t t = (method == 2) ? sz_tans_decompress(ip, stored, scratch, scap)
                                     : sz_split_decompress(ip, stored, scratch, scap);
            if (t == SZ_ERR) goto fail;
            size_t d = sz_decompress_block(scratch, t, op, orig);
            if (d != orig) goto fail;
        } else {
            goto fail;
        }
        ip += stored;
        op += orig;
    }
    free(scratch);
    return (size_t)(op - out);
fail:
    free(scratch);
    return SZ_ERR;
}

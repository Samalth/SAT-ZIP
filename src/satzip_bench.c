#include "satzip_bench.h"
#include <stdlib.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static double now_sec(void){ LARGE_INTEGER f,c; QueryPerformanceFrequency(&f);
                               QueryPerformanceCounter(&c);
                               return (double)c.QuadPart/(double)f.QuadPart; }
#else
  #include <time.h>
  static double now_sec(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
                               return (double)t.tv_sec + (double)t.tv_nsec*1e-9; }
#endif

static size_t satzip_c(void* ctx, const uint8_t* s, size_t n, uint8_t* d) {
    return sz_compress_block((sz_state_t*)ctx, s, n, d, SZ_ACCEL_MAX);
}
static size_t satzip_d(const uint8_t* s, size_t n, uint8_t* d, size_t cap) {
    return sz_decompress_block(s, n, d, cap);
}
static void   satzip_reset(void* ctx) { sz_state_reset((sz_state_t*)ctx); }
static size_t satzip_bound(size_t n)  { return sz_compress_bound(n); }

sz_codec sz_codec_satzip(sz_state_t* st) {
    sz_codec c;
    c.name = "SAT-ZIP"; c.ctx = st; c.reset = satzip_reset;
    c.compress = satzip_c; c.decompress = satzip_d; c.bound = satzip_bound;
    return c;
}

static size_t block_len(size_t off, size_t blk, size_t n) {
    return (off + blk <= n) ? blk : (n - off);
}

int sz_bench_buffer(const sz_codec* codec, const uint8_t* data, size_t n,
                    const sz_bench_opts* opts, sz_progress_cb cb, void* user,
                    sz_bench_result* out) {
    size_t blk  = (opts && opts->block_size) ? opts->block_size : SZ_BLOCK_SIZE;
    int    runs = (opts && opts->runs)       ? opts->runs       : 25;
    size_t nblk = (n + blk - 1) / blk;
    if (nblk == 0) nblk = 1;
    size_t stride = codec->bound(blk) + SZ_MARGIN;

    uint8_t* arena = (uint8_t*)malloc(nblk * stride);
    size_t*  csz   = (size_t*)malloc(nblk * sizeof(size_t));
    uint8_t* raw   = (uint8_t*)calloc(nblk, 1);
    uint8_t* dec   = (uint8_t*)malloc(blk + SZ_MARGIN);
    if (!arena || !csz || !raw || !dec) { free(arena); free(csz); free(raw); free(dec); return -1; }

    double best = 1e30; size_t total_c = 0;
    for (int r = 0; r < runs; ++r) {
        double t0 = now_sec(); total_c = 0;
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = block_len(off, blk, n);
            if (codec->reset) codec->reset(codec->ctx);
            size_t c = codec->compress(codec->ctx, data + off, bn, arena + b*stride);
            if (c == 0) { memcpy(arena + b*stride, data + off, bn); c = bn; raw[b] = 1; }
            else raw[b] = 0;
            csz[b] = c; total_c += c;
        }
        double dt = now_sec() - t0; if (dt < best) best = dt;
    }
    out->comp_mbs   = (double)n / 1e6 / best;
    out->ratio      = (double)n / (double)total_c;
    out->comp_bytes = total_c;

    best = 1e30;
    for (int r = 0; r < runs; ++r) {
        double t0 = now_sec();
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = block_len(off, blk, n);
            if (raw[b]) memcpy(dec, arena + b*stride, bn);
            else        codec->decompress(arena + b*stride, csz[b], dec, bn);
        }
        double dt = now_sec() - t0; if (dt < best) best = dt;
    }
    out->decomp_mbs = (double)n / 1e6 / best;

    if (cb) {
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = block_len(off, blk, n);
            if (codec->reset) codec->reset(codec->ctx);
            double t0 = now_sec();
            size_t c = codec->compress(codec->ctx, data + off, bn, arena + b*stride);
            double dt = now_sec() - t0;
            if (c == 0) { memcpy(arena + b*stride, data + off, bn); c = bn; raw[b] = 1; }
            else raw[b] = 0;
            csz[b] = c;
            cb(user, 0, b, bn, dt > 0 ? (double)bn / 1e6 / dt : 0.0);
        }
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = block_len(off, blk, n);
            double t0 = now_sec();
            if (raw[b]) memcpy(dec, arena + b*stride, bn);
            else        codec->decompress(arena + b*stride, csz[b], dec, bn);
            double dt = now_sec() - t0;
            cb(user, 1, b, bn, dt > 0 ? (double)bn / 1e6 / dt : 0.0);
        }
    }

    int ok = 1;
    for (size_t b = 0; b < nblk && ok; ++b) {
        size_t off = b * blk, bn = block_len(off, blk, n);
        if (raw[b]) { if (memcmp(arena + b*stride, data + off, bn)) ok = 0; }
        else {
            size_t d = codec->decompress(arena + b*stride, csz[b], dec, bn);
            if (d != bn || memcmp(dec, data + off, bn)) ok = 0;
        }
    }
    out->ok = ok; out->in_bytes = n; out->blocks = nblk;

    free(arena); free(csz); free(raw); free(dec);
    return 0;
}

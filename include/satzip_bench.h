#ifndef SATZIP_BENCH_H
#define SATZIP_BENCH_H

#include "satzip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    void*       ctx;
    void        (*reset)(void* ctx);
    size_t      (*compress)(void* ctx, const uint8_t* src, size_t n, uint8_t* dst);
    size_t      (*decompress)(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);
    size_t      (*bound)(size_t n);
} sz_codec;

typedef struct {
    size_t block_size;
    int    runs;
} sz_bench_opts;

typedef struct {
    double comp_mbs, decomp_mbs, ratio;
    size_t in_bytes, comp_bytes, blocks;
    int    ok;
} sz_bench_result;

typedef void (*sz_progress_cb)(void* user, int phase, size_t block_idx,
                               size_t block_bytes, double inst_mbs);

sz_codec sz_codec_satzip(sz_state_t* st);

int sz_bench_buffer(const sz_codec* codec, const uint8_t* data, size_t n,
                    const sz_bench_opts* opts, sz_progress_cb cb, void* user,
                    sz_bench_result* out);

#ifdef __cplusplus
}
#endif
#endif

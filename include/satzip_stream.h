#ifndef SATZIP_STREAM_H
#define SATZIP_STREAM_H

#include "satzip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SZ_STREAM_VERSION 1
#define SZ_FRAME_HDR      16
#define SZ_BLOCK_REC      9
#define SZ_MIN_BLOCK_LOG  12

size_t sz_stream_bound(size_t n, int block_log);

size_t sz_stream_compress(sz_state_t* st, const uint8_t* in, size_t n,
                          uint8_t* out, size_t out_cap, int block_log, int acceleration, int mode);

int sz_stream_header(const uint8_t* in, size_t n, uint64_t* total, int* block_log);

size_t sz_stream_decompress(const uint8_t* in, size_t n, uint8_t* out, size_t out_cap);

#ifdef __cplusplus
}
#endif
#endif

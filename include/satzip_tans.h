#ifndef SATZIP_TANS_H
#define SATZIP_TANS_H

#include "satzip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SZ_TANS_HDR 520

size_t sz_tans_compress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);
size_t sz_tans_decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);

size_t sz_ans_pack(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);
size_t sz_ans_unpack(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);

size_t sz_split_compress(const uint8_t* tokens, size_t tlen, uint8_t* dst, size_t cap);
size_t sz_split_decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap);

#ifdef __cplusplus
}
#endif
#endif

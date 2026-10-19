#ifndef SATZIP_H
#define SATZIP_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(__AVX2__)
  #include <immintrin.h>
  #define SZ_HAVE_AVX2 1
#else
  #define SZ_HAVE_AVX2 0
#endif
#if !SZ_HAVE_AVX2 && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64))
  #include <emmintrin.h>
  #define SZ_HAVE_SSE2 1
#else
  #define SZ_HAVE_SSE2 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SZ_BLOCK_LOG   16
#define SZ_BLOCK_SIZE  (1u << SZ_BLOCK_LOG)
#define SZ_HLOG        14
#define SZ_MIN_MATCH   4
#define SZ_MAX_OFFSET  (SZ_BLOCK_SIZE - 1)
#define SZ_LAST_LITS   16
#define SZ_MARGIN      64

#if defined(__GNUC__) || defined(__clang__)
  #define SZ_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define SZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define SZ_INLINE      static inline __attribute__((always_inline))
#else
  #define SZ_LIKELY(x)   (x)
  #define SZ_UNLIKELY(x) (x)
  #define SZ_INLINE      static __forceinline
#endif

#define sz_compress_bound(n) ((size_t)(n) + (size_t)(n) / 255u + 64u)

#define SZ_ERR ((size_t)-1)

typedef struct {
    uint32_t table[1u << SZ_HLOG];
    uint16_t chain[SZ_BLOCK_SIZE];
} sz_state_t;

SZ_INLINE void sz_state_reset(sz_state_t* st) { memset(st->table, 0, sizeof(st->table)); }

SZ_INLINE uint64_t sz_load64(const void* p) { uint64_t v; memcpy(&v, p, 8); return v; }
SZ_INLINE uint32_t sz_load32(const void* p) { uint32_t v; memcpy(&v, p, 4); return v; }

SZ_INLINE unsigned sz_ctz32(uint32_t x) {
#if defined(_MSC_VER)
    unsigned long i; _BitScanForward(&i, x); return (unsigned)i;
#else
    return (unsigned)__builtin_ctz(x);
#endif
}
SZ_INLINE unsigned sz_ctz64(uint64_t x) {
#if defined(_MSC_VER)
    unsigned long i; _BitScanForward64(&i, x); return (unsigned)i;
#else
    return (unsigned)__builtin_ctzll(x);
#endif
}

SZ_INLINE uint32_t sz_hash5(uint64_t x) {
    return (uint32_t)(((x << 24) * 889523592379ULL) >> (64 - SZ_HLOG));
}

SZ_INLINE uint32_t sz_hash8(uint64_t x) {
    return (uint32_t)((x * 0x9E3779B185EBCA87ULL) >> (64 - SZ_HLOG));
}

SZ_INLINE size_t sz_match_len(const uint8_t* a, const uint8_t* b, const uint8_t* a_limit) {
    const uint8_t* const a0 = a;
#if SZ_HAVE_AVX2
    while (SZ_LIKELY(a + 32 <= a_limit)) {
        __m256i va = _mm256_loadu_si256((const __m256i*)a);
        __m256i vb = _mm256_loadu_si256((const __m256i*)b);
        uint32_t neq = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(va, vb));
        if (neq) return (size_t)(a - a0) + sz_ctz32(neq);
        a += 32; b += 32;
    }
#elif SZ_HAVE_SSE2
    while (SZ_LIKELY(a + 16 <= a_limit)) {
        __m128i va = _mm_loadu_si128((const __m128i*)a);
        __m128i vb = _mm_loadu_si128((const __m128i*)b);
        uint32_t neq = ~(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb)) & 0xFFFFu;
        if (neq) return (size_t)(a - a0) + sz_ctz32(neq);
        a += 16; b += 16;
    }
#endif
    while (a + 8 <= a_limit) {
        uint64_t x = sz_load64(a) ^ sz_load64(b);
        if (x) return (size_t)(a - a0) + (sz_ctz64(x) >> 3);
        a += 8; b += 8;
    }
    while (a < a_limit && *a == *b) { ++a; ++b; }
    return (size_t)(a - a0);
}

SZ_INLINE void sz_wildcopy(uint8_t* d, const uint8_t* s, uint8_t* d_end) {
#if SZ_HAVE_AVX2
    do {
        _mm256_storeu_si256((__m256i*)d, _mm256_loadu_si256((const __m256i*)s));
        d += 32; s += 32;
    } while (d < d_end);
#elif SZ_HAVE_SSE2
    do {
        _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s));
        d += 16; s += 16;
    } while (d < d_end);
#else
    do { memcpy(d, s, 8); d += 8; s += 8; } while (d < d_end);
#endif
}

#define SZ_ACCEL_MAX 1

typedef enum { SZ_MODE_S = 0, SZ_MODE_R = 1, SZ_MODE_T = 2 } sz_mode;

size_t sz_compress_block(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                         int acceleration);
size_t sz_compress_block_r(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                           int acceleration);

size_t sz_decompress_block(const uint8_t* src, size_t src_len,
                           uint8_t* dst, size_t dst_capacity);

const char* sz_cpu_path(void);

#ifdef __cplusplus
}
#endif
#endif

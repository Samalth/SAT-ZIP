#include "satzip.h"

#include <stdlib.h>
#if defined(_MSC_VER)
  #include <intrin.h>
#endif

typedef size_t (*sz_compress_fn)(sz_state_t*, const uint8_t*, size_t, uint8_t*, int);
typedef size_t (*sz_decompress_fn)(const uint8_t*, size_t, uint8_t*, size_t);

size_t sz_compress_block_base(sz_state_t*, const uint8_t*, size_t, uint8_t*, int);
size_t sz_compress_block_r_base(sz_state_t*, const uint8_t*, size_t, uint8_t*, int);
size_t sz_decompress_block_base(const uint8_t*, size_t, uint8_t*, size_t);

#if defined(SATZIP_DISPATCH_AVX2)
size_t sz_compress_block_avx2(sz_state_t*, const uint8_t*, size_t, uint8_t*, int);
size_t sz_compress_block_r_avx2(sz_state_t*, const uint8_t*, size_t, uint8_t*, int);
size_t sz_decompress_block_avx2(const uint8_t*, size_t, uint8_t*, size_t);

static int sz_cpu_has_avx2(void) {
    const char* off = getenv("SATZIP_NO_AVX2");
    if (off && *off && *off != '0') return 0;
#if defined(_MSC_VER)
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return 0;
    __cpuid(r, 1);
    if (!(r[2] & (1 << 27)) || !(r[2] & (1 << 28)) || !(r[2] & (1 << 12))) return 0;
    if ((_xgetbv(0) & 6) != 6) return 0;
    __cpuidex(r, 7, 0);
    if (!(r[1] & (1 << 5)) || !(r[1] & (1 << 3)) || !(r[1] & (1 << 8))) return 0;
    __cpuid(r, (int)0x80000000);
    if ((unsigned)r[0] < 0x80000001u) return 0;
    __cpuid(r, (int)0x80000001);
    return (r[2] & (1 << 5)) != 0;
#else
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi") &&
           __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("fma");
#endif
}
#endif

static sz_compress_fn   g_compress;
static sz_compress_fn   g_compress_r;
static sz_decompress_fn g_decompress;
static const char*      g_path;

static void sz_resolve(void) {
#if defined(SATZIP_DISPATCH_AVX2)
    if (sz_cpu_has_avx2()) {
        g_path       = "AVX2";
        g_compress_r = sz_compress_block_r_avx2;
        g_decompress = sz_decompress_block_avx2;
        g_compress   = sz_compress_block_avx2;
        return;
    }
#endif
    g_path       = SZ_HAVE_SSE2 ? "SSE2" : "generic";
    g_compress_r = sz_compress_block_r_base;
    g_decompress = sz_decompress_block_base;
    g_compress   = sz_compress_block_base;
}

size_t sz_compress_block(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                         int acceleration) {
    if (SZ_UNLIKELY(!g_compress)) sz_resolve();
    return g_compress(st, in, in_len, out, acceleration);
}

size_t sz_compress_block_r(sz_state_t* st, const uint8_t* in, size_t in_len, uint8_t* out,
                           int acceleration) {
    if (SZ_UNLIKELY(!g_compress_r)) sz_resolve();
    return g_compress_r(st, in, in_len, out, acceleration);
}

size_t sz_decompress_block(const uint8_t* src, size_t src_len,
                           uint8_t* dst, size_t dst_capacity) {
    if (SZ_UNLIKELY(!g_decompress)) sz_resolve();
    return g_decompress(src, src_len, dst, dst_capacity);
}

const char* sz_cpu_path(void) {
    if (!g_path) sz_resolve();
    return g_path;
}

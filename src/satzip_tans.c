#include "satzip_tans.h"

#define SZ_ANS_TL    12
#define SZ_ANS_TSIZE (1u << SZ_ANS_TL)
#define SZ_ANS_LOW   (1u << 23)

static void ans_wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ans_rd32(const uint8_t* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i); return v; }

static int ans_normalize(const uint32_t* cnt, size_t total, uint16_t* norm) {
    uint32_t sum = 0, maxv = 0;
    int maxs = -1;
    for (int s = 0; s < 256; ++s) {
        norm[s] = 0;
        if (!cnt[s]) continue;
        uint32_t v = (uint32_t)(((uint64_t)cnt[s] * SZ_ANS_TSIZE) / total);
        if (!v) v = 1;
        norm[s] = (uint16_t)v;
        sum += v;
        if (cnt[s] > maxv) { maxv = cnt[s]; maxs = s; }
    }
    if (maxs < 0) return -1;
    int nv = (int)norm[maxs] + ((int)SZ_ANS_TSIZE - (int)sum);
    if (nv < 1) return -1;
    norm[maxs] = (uint16_t)nv;
    return 0;
}

static size_t ans_encode_tail(const uint8_t* src, size_t n, const uint16_t* norm,
                              const uint32_t* cum, uint8_t* lim, uint8_t* end, uint32_t* state) {
    uint8_t* p = end;
    uint32_t x = SZ_ANS_LOW;
    for (size_t i = n; i-- > 0;) {
        uint8_t  s = src[i];
        uint32_t f = norm[s];
        uint32_t xmax = f << (23 - SZ_ANS_TL + 8);
        while (x >= xmax) {
            if (p <= lim) return SZ_ERR;
            *--p = (uint8_t)x;
            x >>= 8;
        }
        x = ((x / f) << SZ_ANS_TL) + (x % f) + cum[s];
    }
    *state = x;
    return (size_t)(end - p);
}

static size_t ans_decode(const uint16_t* norm, const uint32_t* cum, uint32_t count, uint32_t x,
                         const uint8_t* p, const uint8_t* pe, uint8_t* dst, size_t cap) {
    if (count > cap) return SZ_ERR;
    uint8_t s2s[SZ_ANS_TSIZE];
    for (int s = 0; s < 256; ++s)
        for (uint32_t k = cum[s]; k < cum[s + 1]; ++k) s2s[k] = (uint8_t)s;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = x & (SZ_ANS_TSIZE - 1);
        uint8_t  s    = s2s[slot];
        dst[i] = s;
        x = norm[s] * (x >> SZ_ANS_TL) + slot - cum[s];
        while (x < SZ_ANS_LOW) {
            if (p >= pe) return SZ_ERR;
            x = (x << 8) | *p++;
        }
    }
    if (x != SZ_ANS_LOW || p != pe) return SZ_ERR;
    return count;
}

size_t sz_tans_compress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (!n || n > 0xFFFFFFFFu || cap <= SZ_TANS_HDR) return 0;

    uint32_t cnt[256] = {0};
    for (size_t i = 0; i < n; ++i) cnt[src[i]]++;
    uint16_t norm[256];
    if (ans_normalize(cnt, n, norm) != 0) return 0;
    uint32_t cum[257];
    cum[0] = 0;
    for (int s = 0; s < 256; ++s) cum[s + 1] = cum[s] + norm[s];

    uint32_t x = 0;
    size_t payload = ans_encode_tail(src, n, norm, cum, dst + SZ_TANS_HDR, dst + cap, &x);
    if (payload == SZ_ERR) return 0;
    size_t total = SZ_TANS_HDR + payload;
    if (total >= n) return 0;

    for (int s = 0; s < 256; ++s) { dst[2 * s] = (uint8_t)norm[s]; dst[2 * s + 1] = (uint8_t)(norm[s] >> 8); }
    ans_wr32(dst + 512, (uint32_t)n);
    ans_wr32(dst + 516, x);
    memmove(dst + SZ_TANS_HDR, dst + cap - payload, payload);
    return total;
}

size_t sz_tans_decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (n < SZ_TANS_HDR) return SZ_ERR;
    uint16_t norm[256];
    uint32_t cum[257];
    cum[0] = 0;
    for (int s = 0; s < 256; ++s) {
        norm[s] = (uint16_t)(src[2 * s] | (src[2 * s + 1] << 8));
        cum[s + 1] = cum[s] + norm[s];
    }
    if (cum[256] != SZ_ANS_TSIZE) return SZ_ERR;
    uint32_t count = ans_rd32(src + 512);
    uint32_t x     = ans_rd32(src + 516);
    return ans_decode(norm, cum, count, x, src + SZ_TANS_HDR, src + n, dst, cap);
}

size_t sz_ans_pack(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (!n || n > 0xFFFFFFFFu || cap < 16) return 0;

    uint32_t cnt[256] = {0};
    for (size_t i = 0; i < n; ++i) cnt[src[i]]++;
    uint16_t norm[256];
    if (ans_normalize(cnt, n, norm) != 0) return 0;
    uint32_t cum[257];
    cum[0] = 0;
    int nsym = 0;
    for (int s = 0; s < 256; ++s) { cum[s + 1] = cum[s] + norm[s]; if (norm[s]) ++nsym; }

    size_t hdr = 2 + (size_t)nsym * 3 + 4 + 4;
    if (hdr >= cap) return 0;

    uint32_t x = 0;
    size_t payload = ans_encode_tail(src, n, norm, cum, dst + hdr, dst + cap, &x);
    if (payload == SZ_ERR) return 0;
    size_t total = hdr + payload;
    if (total >= n) return 0;

    uint8_t* h = dst;
    h[0] = (uint8_t)nsym; h[1] = (uint8_t)(nsym >> 8);
    h += 2;
    for (int s = 0; s < 256; ++s) {
        if (!norm[s]) continue;
        h[0] = (uint8_t)s; h[1] = (uint8_t)norm[s]; h[2] = (uint8_t)(norm[s] >> 8);
        h += 3;
    }
    ans_wr32(h, (uint32_t)n);
    ans_wr32(h + 4, x);
    memmove(dst + hdr, dst + cap - payload, payload);
    return total;
}

size_t sz_ans_unpack(const uint8_t* src, size_t n, uint8_t* dst, size_t cap) {
    if (n < 10) return SZ_ERR;
    int nsym = src[0] | (src[1] << 8);
    if (nsym < 1 || nsym > 256) return SZ_ERR;
    size_t hdr = 2 + (size_t)nsym * 3 + 8;
    if (n < hdr) return SZ_ERR;

    uint16_t norm[256] = {0};
    const uint8_t* h = src + 2;
    for (int i = 0; i < nsym; ++i, h += 3) {
        uint16_t f = (uint16_t)(h[1] | (h[2] << 8));
        if (norm[h[0]]) return SZ_ERR;
        norm[h[0]] = f;
    }
    uint32_t cum[257];
    cum[0] = 0;
    for (int s = 0; s < 256; ++s) cum[s + 1] = cum[s] + norm[s];
    if (cum[256] != SZ_ANS_TSIZE) return SZ_ERR;

    uint32_t count = ans_rd32(h);
    uint32_t x     = ans_rd32(h + 4);
    return ans_decode(norm, cum, count, x, src + hdr, src + n, dst, cap);
}

#include "satzip.h"
#include "satzip_stream.h"
#include "satzip_archive.h"
#include "satzip_tans.h"
#include <stdio.h>
#include <stdlib.h>

static uint64_t g_rng = 0x243F6A8885A308D3ULL;
static uint32_t xrand(void) {
    uint64_t x = g_rng;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    g_rng = x;
    return (uint32_t)(x >> 32);
}

static sz_state_t g_state;
static int g_fail = 0, g_pass = 0, g_raw = 0;

static int check(const uint8_t* in, size_t n, const char* name) {
    size_t cbound = sz_compress_bound(n) + SZ_MARGIN;
    uint8_t* comp = (uint8_t*)malloc(cbound);
    uint8_t* deco = (uint8_t*)malloc(n + SZ_MARGIN);
    int ok = 1;

    sz_state_reset(&g_state);
    size_t rc = sz_compress_block_r(&g_state, in, n, comp, 1);
    if (rc != 0) {
        size_t rd = sz_decompress_block(comp, rc, deco, n);
        if (rd != n || memcmp(in, deco, n) != 0) {
            printf("  FAIL %-22s mode R round-trip\n", name);
            free(comp); free(deco); g_fail++; return 0;
        }
    }

    sz_state_reset(&g_state);
    int accel = 1 + (int)(xrand() % 9);
    size_t csize = sz_compress_block(&g_state, in, n, comp, accel);

    if (csize == 0) {
        g_raw++;
    } else {
        size_t dsize = sz_decompress_block(comp, csize, deco, n);
        if (dsize != n) {
            printf("  FAIL %-22s csize=%zu decoded=%zu expected=%zu\n", name, csize, dsize, n);
            ok = 0;
        } else if (memcmp(in, deco, n) != 0) {
            size_t i = 0; while (i < n && in[i] == deco[i]) ++i;
            printf("  FAIL %-22s mismatch at byte %zu (0x%02x vs 0x%02x)\n",
                   name, i, in[i], deco[i]);
            ok = 0;
        }
    }
    free(comp); free(deco);
    if (ok) g_pass++; else g_fail++;
    return ok;
}

int main(void) {
    enum { CAP = SZ_BLOCK_SIZE };
    uint8_t* buf = (uint8_t*)malloc(CAP + SZ_MARGIN);

    for (size_t n = 0; n <= 4096; n += 257) {
        memset(buf, 0, n); check(buf, n, "zeros");
    }

    for (int t = 0; t < 200; ++t) {
        size_t n = 1 + (xrand() % CAP);
        for (size_t i = 0; i < n + SZ_MARGIN; ++i) buf[i] = (uint8_t)xrand();
        check(buf, n, "random");
    }

    for (uint32_t period = 1; period <= 7; ++period) {
        size_t n = 5000;
        for (size_t i = 0; i < n + SZ_MARGIN; ++i) buf[i] = (uint8_t)((i % period) + 'A');
        check(buf, n, "small-period");
    }

    for (size_t n = 16; n < 16 + 300; ++n) {
        for (size_t i = 0; i < n + SZ_MARGIN; ++i) buf[i] = (uint8_t)(i & 0x0F);
        check(buf, n, "nibble-boundary");
    }

    {
        static const char* w[] = {"the ", "quick ", "brown ", "fox ", "jumps ",
                                   "over ", "lazy ", "dog ", "and ", "then "};
        size_t n = 0;
        while (n < CAP - 16) {
            const char* s = w[xrand() % 10];
            size_t l = strlen(s);
            memcpy(buf + n, s, l); n += l;
        }
        memset(buf + n, 0, SZ_MARGIN);
        check(buf, n, "text");
    }

    for (int t = 0; t < 2000; ++t) {
        size_t n = 1 + (xrand() % CAP);
        size_t i = 0;
        while (i < n) {
            if (xrand() & 1) {
                uint8_t v = (uint8_t)xrand();
                size_t run = 1 + (xrand() % 64);
                while (run-- && i < n) buf[i++] = v;
            } else {
                size_t run = 1 + (xrand() % 32);
                while (run-- && i < n) buf[i++] = (uint8_t)xrand();
            }
        }
        for (size_t k = 0; k < SZ_MARGIN; ++k) buf[n + k] = 0;
        check(buf, n, "fuzz");
    }

    free(buf);

    {
        enum { SCAP = 300u * 1024 };
        uint8_t* sin  = (uint8_t*)malloc(SCAP + SZ_MARGIN);
        uint8_t* sout = (uint8_t*)malloc(sz_stream_bound(SCAP, 16));
        uint8_t* sdec = (uint8_t*)malloc(SCAP + SZ_MARGIN);
        sz_state_t* st = (sz_state_t*)calloc(1, sizeof(sz_state_t));

        size_t sizes[] = {0, 1, 100, 65535, 65536, 65537, 131072, 200003, SCAP};
        for (size_t si = 0; si < sizeof(sizes)/sizeof(sizes[0]); ++si) {
            size_t n = sizes[si];
            for (int kind = 0; kind < 4; ++kind) {
                for (size_t i = 0; i < n; ++i) {
                    switch (kind) {
                        case 0: sin[i] = 0; break;
                        case 1: sin[i] = (uint8_t)('A' + (i % 16)); break;
                        case 2: sin[i] = (uint8_t)((xrand() & 1) ? (i & 7) : xrand()); break;
                        default: sin[i] = (uint8_t)xrand(); break;
                    }
                }
                memset(sin + n, 0, SZ_MARGIN);

                for (int mode = SZ_MODE_R; mode <= SZ_MODE_T; ++mode) {
                    sz_state_reset(st);
                    size_t c = sz_stream_compress(st, sin, n, sout, sz_stream_bound(n,16), 16, 1, mode);
                    int ok = (c != 0) || (n == 0 && c >= SZ_FRAME_HDR);
                    uint64_t total = (uint64_t)-1;
                    if (ok && sz_stream_header(sout, c, &total, NULL) != 0) ok = 0;
                    if (ok && total != n) ok = 0;
                    if (ok) {
                        size_t d = sz_stream_decompress(sout, c, sdec, n);
                        if (d != n || (n && memcmp(sin, sdec, n))) ok = 0;
                    }
                    if (ok) g_pass++; else { g_fail++; printf("  FAIL stream n=%zu kind=%d mode=%d\n", n, kind, mode); }
                }
            }
        }
        free(sin); free(sout); free(sdec); free(st);
    }

    {
        enum { SZ = 70000 };
        const char* names[] = {"hello.txt", "sub/", "sub/rand.bin", "sub/zero.dat", "empty"};
        const int   isdir[] = {0,           1,      0,              0,             0};
        uint8_t* fdata[5] = {0};
        size_t   fsize[5];
        sz_state_t* st = (sz_state_t*)calloc(1, sizeof(sz_state_t));

        for (int i = 0; i < 5; ++i) {
            if (isdir[i]) { fsize[i] = 0; continue; }
            size_t n = (i == 4) ? 0 : SZ;
            fdata[i] = (uint8_t*)malloc(n + SZ_MARGIN);
            for (size_t k = 0; k < n; ++k) {
                if (i == 2) fdata[i][k] = (uint8_t)xrand();
                else if (i == 3) fdata[i][k] = 0;
                else fdata[i][k] = (uint8_t)('a' + (k % 26));
            }
            memset(fdata[i] + n, 0, SZ_MARGIN);
            fsize[i] = n;
        }

        size_t bound = SZ_AR_HEADER + SZ_AR_END + SZ_MARGIN;
        for (int i = 0; i < 5; ++i) { bound += SZ_AR_DIR_BOUND + strlen(names[i]);
                                      if (!isdir[i]) bound += sz_stream_bound(fsize[i], 16); }
        uint8_t* arc = (uint8_t*)malloc(bound);

        sz_archive_writer w; sz_aw_init(&w, arc, bound);
        for (int i = 0; i < 5; ++i) sz_aw_add(&w, names[i], isdir[i], fdata[i], fsize[i], st, 1, SZ_MODE_R);
        size_t alen = sz_aw_finish(&w);

        int ok = (alen != 0);
        sz_ar_entry* es = NULL; uint32_t cnt = 0;
        if (ok && sz_ar_open(arc, alen, &es, &cnt) != 0) ok = 0;
        if (ok && cnt != 5) ok = 0;
        if (ok) {
            for (uint32_t e = 0; e < cnt && ok; ++e) {
                int i = -1;
                for (int j = 0; j < 5; ++j) if (!strcmp(names[j], es[e].name)) i = j;
                if (i < 0) { ok = 0; break; }
                if (es[e].is_dir != (isdir[i] != 0)) { ok = 0; break; }
                if (es[e].is_dir) continue;
                uint8_t* obuf = (uint8_t*)malloc(fsize[i] + SZ_MARGIN);
                size_t d = sz_ar_extract(arc, alen, &es[e], obuf, fsize[i]);
                if (d != fsize[i] || (fsize[i] && memcmp(obuf, fdata[i], fsize[i]))) ok = 0;
                free(obuf);
            }
        }
        if (ok) g_pass++; else { g_fail++; printf("  FAIL archive\n"); }

        sz_ar_close(es, cnt);
        for (int i = 0; i < 5; ++i) free(fdata[i]);
        free(arc); free(st);
    }

    {
        enum { ACAP = 100000 };
        uint8_t* ain = (uint8_t*)malloc(ACAP);
        uint8_t* ac  = (uint8_t*)malloc(ACAP + 4096);
        uint8_t* ad  = (uint8_t*)malloc(ACAP);
        size_t sizes[] = {1, 100, 600, 4096, 65536, ACAP};
        for (size_t si = 0; si < sizeof(sizes)/sizeof(sizes[0]); ++si) {
            size_t n = sizes[si];
            for (int kind = 0; kind < 4; ++kind) {
                for (size_t i = 0; i < n; ++i) {
                    switch (kind) {
                        case 0: ain[i] = 'x'; break;
                        case 1: ain[i] = (uint8_t)('a' + (i % 20)); break;
                        case 2: ain[i] = (uint8_t)((xrand() & 3) ? 'e' : (uint8_t)xrand()); break;
                        default: ain[i] = (uint8_t)xrand(); break;
                    }
                }
                size_t c = sz_tans_compress(ain, n, ac, ACAP + 4096);
                if (c == 0) { g_raw++; continue; }
                size_t d = sz_tans_decompress(ac, c, ad, n);
                if (d == n && memcmp(ain, ad, n) == 0) g_pass++;
                else { g_fail++; printf("  FAIL tans n=%zu kind=%d\n", n, kind); }
            }
        }
        free(ain); free(ac); free(ad);
    }

    printf("\nSAT-ZIP round-trip: %d passed, %d raw(stored), %d FAILED\n",
           g_pass, g_raw, g_fail);
    return g_fail ? 1 : 0;
}

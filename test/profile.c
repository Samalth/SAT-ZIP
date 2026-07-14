#include "satzip.h"
#include "satzip_tans.h"
#include <stdio.h>
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

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: profile <file> [s|r|t] [acceleration]\n"); return 2; }
    char mc = (argc >= 3) ? (char)(argv[2][0] | 32) : 's';
    int mode = (mc == 'r') ? SZ_MODE_R : (mc == 't') ? SZ_MODE_T : SZ_MODE_S;
    int accel = (argc >= 4) ? atoi(argv[3]) : 1;
    printf("mode: %c\n", mc - 32);
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz + SZ_MARGIN);
    size_t got = fread(buf, 1, (size_t)sz, f); fclose(f);
    memset(buf + got, 0, SZ_MARGIN);

    size_t blk = SZ_BLOCK_SIZE;
    size_t nblk = (got + blk - 1) / blk;
    uint8_t* out  = (uint8_t*)malloc(sz_compress_bound(blk) + SZ_MARGIN);
    uint8_t* out2 = (uint8_t*)malloc(sz_compress_bound(blk) + SZ_MARGIN);
    sz_state_t* st = (sz_state_t*)calloc(1, sizeof(sz_state_t));

    printf("block     bytes    MB/s  ratio     csize\n");
    size_t tot_in = 0, tot_c = 0; double tot_t = 0;
    for (size_t b = 0; b < nblk; ++b) {
        size_t off = b * blk;
        size_t bn = (off + blk <= got) ? blk : (got - off);
        size_t c = 0; double best = 1e30;
        for (int r = 0; r < 9; ++r) {
            sz_state_reset(st);
            double t0 = now_sec();
            if (mode == SZ_MODE_S) c = sz_compress_block(st, buf + off, bn, out, accel);
            else {
                c = sz_compress_block_r(st, buf + off, bn, out, accel);
                if (mode == SZ_MODE_T && c) {
                    size_t a = sz_split_compress(out, c, out2, sz_compress_bound(blk));
                    if (a && a < c) c = a;
                }
            }
            double dt = now_sec() - t0; if (dt < best) best = dt;
        }
        if (c == 0) c = bn;
        double mbs = (double)bn / 1e6 / best;
        printf("%5zu  %8zu  %6.0f  %5.2f  %8zu%s\n",
               b, bn, mbs, (double)bn / (double)c, c, mbs < 4000 ? "   <== SLOW" : "");
        tot_in += bn; tot_c += c; tot_t += best;
    }
    printf("total %zu -> %zu (x%.3f)  %.0f MB/s\n",
           tot_in, tot_c, (double)tot_in / (double)tot_c, (double)tot_in / 1e6 / tot_t);
    free(buf); free(out); free(out2); free(st);
    return 0;
}

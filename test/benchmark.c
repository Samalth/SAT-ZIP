#include "satzip_bench.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_LZ4)
  #include "lz4.h"
  static size_t lz4_c(void* ctx, const uint8_t* s, size_t n, uint8_t* d) {
      (void)ctx; return (size_t)LZ4_compress_default((const char*)s,(char*)d,(int)n,
                                                      LZ4_compressBound((int)n));
  }
  static size_t lz4_d(const uint8_t* s, size_t n, uint8_t* d, size_t cap) {
      int r = LZ4_decompress_safe((const char*)s,(char*)d,(int)n,(int)cap);
      return r < 0 ? SZ_ERR : (size_t)r;
  }
  static size_t lz4_bound(size_t n) { return (size_t)LZ4_compressBound((int)n); }
  static sz_codec codec_lz4(void) {
      sz_codec c; c.name="LZ4"; c.ctx=NULL; c.reset=NULL;
      c.compress=lz4_c; c.decompress=lz4_d; c.bound=lz4_bound; return c;
  }
#endif

static uint64_t g_rng = 0x243F6A8885A308D3ULL;
static uint32_t xrand(void){ uint64_t x=g_rng; x^=x<<13; x^=x>>7; x^=x<<17; g_rng=x; return (uint32_t)(x>>32); }

static void run_corpus(const char* name, const uint8_t* in, size_t n) {
    sz_state_t st = {0}; sz_state_reset(&st);
    sz_codec sat = sz_codec_satzip(&st);
    sz_bench_result s; sz_bench_buffer(&sat, in, n, NULL, NULL, NULL, &s);
    printf("  %-16s %6.2f MB | SAT-ZIP %7.0f / %7.0f MB/s  x%.2f %s",
           name, (double)n/1e6, s.comp_mbs, s.decomp_mbs, s.ratio, s.ok?"":"[DECODE FAIL]");
#if defined(HAVE_LZ4)
    sz_codec l4 = codec_lz4();
    sz_bench_result l; sz_bench_buffer(&l4, in, n, NULL, NULL, NULL, &l);
    printf("  | LZ4 %7.0f / %7.0f MB/s  x%.2f %s",
           l.comp_mbs, l.decomp_mbs, l.ratio, l.ok?"":"[FAIL]");
#endif
    printf("\n");
}

static size_t gen_text(uint8_t* b, size_t cap) {
    static const char* w[]={"the ","quick ","brown ","fox ","jumps ","over ","lazy ",
                            "dog ","while ","the ","cat ","sat ","on ","the ","mat "};
    size_t n=0; while (n < cap-16){ const char* s=w[xrand()%15]; size_t l=strlen(s);
        memcpy(b+n,s,l); n+=l; } return n;
}
static size_t gen_mixed(uint8_t* b, size_t cap) {
    size_t i=0; while (i<cap){ if (xrand()&1){ uint8_t v=(uint8_t)xrand(); size_t run=1+(xrand()%80);
        while(run--&&i<cap) b[i++]=v; } else { size_t run=1+(xrand()%24);
        while(run--&&i<cap) b[i++]=(uint8_t)xrand(); } } return cap;
}
static size_t gen_random(uint8_t* b, size_t cap){ for(size_t i=0;i<cap;i++) b[i]=(uint8_t)xrand(); return cap; }
static size_t gen_zeros(uint8_t* b, size_t cap){ memset(b,0,cap); return cap; }

int main(int argc, char** argv) {
#if defined(HAVE_LZ4)
    printf("SAT-ZIP vs LZ4 %s  (64 KiB blocks, best-of-25, per-block reset timed)\n\n",
           LZ4_versionString());
#else
    printf("SAT-ZIP benchmark (LZ4 not linked)  64 KiB blocks, best-of-25\n\n");
#endif
    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t* buf = (uint8_t*)malloc((size_t)sz + SZ_MARGIN);
        size_t got = fread(buf, 1, (size_t)sz, f); fclose(f);
        memset(buf + got, 0, SZ_MARGIN);
        run_corpus(argv[1], buf, got);
        free(buf);
        return 0;
    }
    const size_t CAP = 16u*1024*1024;
    uint8_t* buf = (uint8_t*)malloc(CAP + SZ_MARGIN);
    size_t n;
    n = gen_text(buf, CAP);   memset(buf+n,0,SZ_MARGIN); run_corpus("text",   buf, n);
    n = gen_mixed(buf, CAP);  memset(buf+n,0,SZ_MARGIN); run_corpus("mixed",  buf, n);
    n = gen_zeros(buf, CAP);  memset(buf+n,0,SZ_MARGIN); run_corpus("zeros",  buf, n);
    n = gen_random(buf, CAP); memset(buf+n,0,SZ_MARGIN); run_corpus("random", buf, n);
    free(buf);
    return 0;
}

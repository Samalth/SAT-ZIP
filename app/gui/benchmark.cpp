#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

#include "benchmark.hpp"
#include "satzip_tans.h"
#include "satzip_zip.hpp"
#include "satzip_7z.hpp"
#if defined(SATZIP_HAVE_7Z)
#include "LzmaEnc.h"
#include "LzmaDec.h"
#endif
#if defined(HAVE_LZ4)
  #include "lz4.h"
#endif

#include <chrono>
#include <cstring>
#include <functional>

namespace szgui {

#if defined(HAVE_LZ4)
static size_t lz4_c(void*, const uint8_t* s, size_t n, uint8_t* d) {
    return (size_t)LZ4_compress_default((const char*)s, (char*)d, (int)n, LZ4_compressBound((int)n));
}
static size_t lz4_d(const uint8_t* s, size_t n, uint8_t* d, size_t cap) {
    int r = LZ4_decompress_safe((const char*)s, (char*)d, (int)n, (int)cap); return r < 0 ? SZ_ERR : (size_t)r;
}
static size_t lz4_bound(size_t n) { return (size_t)LZ4_compressBound((int)n); }
#endif
struct Codec3 {
    const char* name;
    std::function<size_t(const uint8_t*, size_t, uint8_t*)>        compress;
    std::function<size_t(const uint8_t*, size_t, uint8_t*, size_t)> decompress;
    std::function<size_t(size_t)>                                  bound;
};

static double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void bench_parallel(App* a, const Codec3& c, CodecView* view,
                           const uint8_t* d, size_t n, size_t blk, int runs, int threads) {
    size_t nblk = (n + blk - 1) / blk; if (!nblk) nblk = 1;
    size_t stride = c.bound(blk) + SZ_MARGIN;
    std::vector<uint8_t> arena(nblk * stride);
    std::vector<size_t>  csz(nblk, 0);
    std::vector<char>    raw(nblk, 0);
    auto blen = [&](size_t b) { size_t off = b * blk; return (off + blk <= n) ? blk : (n - off); };

    double best = 1e30;
    for (int r = 0; r < runs && !a->cancel; ++r) {
        double t0 = now_s();
        sz::parallel_for(nblk, threads, [&](size_t b) {
            size_t off = b * blk, bn = blen(b);
            size_t cc = c.compress(d + off, bn, arena.data() + b * stride);
            if (!cc || cc >= bn) { memcpy(arena.data() + b * stride, d + off, bn); cc = bn; raw[b] = 1; }
            else raw[b] = 0;
            csz[b] = cc;
        }, &a->cancel);
        double dt = now_s() - t0; if (dt < best) best = dt;
    }
    if (a->cancel) return;
    size_t total_c = 0; for (size_t b = 0; b < nblk; ++b) total_c += csz[b];

    double bestd = 1e30;
    for (int r = 0; r < runs && !a->cancel; ++r) {
        double t0 = now_s();
        sz::parallel_for(nblk, threads, [&](size_t b) {
            static thread_local std::vector<uint8_t> dec;
            size_t off = b * blk, bn = blen(b);
            if (dec.size() < bn + SZ_MARGIN) dec.resize(blk + SZ_MARGIN);
            if (raw[b]) memcpy(dec.data(), arena.data() + b * stride, bn);
            else        c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
        }, &a->cancel);
        double dt = now_s() - t0; if (dt < bestd) bestd = dt;
    }
    if (a->cancel) return;

    { std::lock_guard<std::mutex> lk(a->mtx); view->comp.clear(); view->decomp.clear(); }
    if (threads <= 1) {
        std::vector<uint8_t> scratch(stride);
        std::vector<uint8_t> dec(blk + SZ_MARGIN);
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = blen(b);
            double t0 = now_s(); c.compress(d + off, bn, scratch.data()); double dt = now_s() - t0;
            std::lock_guard<std::mutex> lk(a->mtx);
            view->comp.x.push_back((float)b); view->comp.y.push_back(dt > 0 ? (float)(bn / 1e6 / dt) : 0.0f);
        }
        for (size_t b = 0; b < nblk; ++b) {
            size_t off = b * blk, bn = blen(b);
            double t0 = now_s();
            if (raw[b]) memcpy(dec.data(), arena.data() + b * stride, bn);
            else        c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
            double dt = now_s() - t0;
            std::lock_guard<std::mutex> lk(a->mtx);
            view->decomp.x.push_back((float)b); view->decomp.y.push_back(dt > 0 ? (float)(bn / 1e6 / dt) : 0.0f);
        }
    }

    bool ok = true;
    for (size_t b = 0; b < nblk && ok; ++b) {
        size_t off = b * blk, bn = blen(b);
        std::vector<uint8_t> dec(bn + SZ_MARGIN);
        if (raw[b]) { if (memcmp(arena.data() + b * stride, d + off, bn)) ok = false; }
        else { size_t dd = c.decompress(arena.data() + b * stride, csz[b], dec.data(), bn);
               if (dd != bn || memcmp(dec.data(), d + off, bn)) ok = false; }
    }

    sz_bench_result r{};
    r.comp_mbs = n / 1e6 / best; r.decomp_mbs = n / 1e6 / bestd;
    r.ratio = total_c ? (double)n / (double)total_c : 0.0;
    r.comp_bytes = total_c; r.in_bytes = n; r.blocks = nblk; r.ok = ok;
    std::lock_guard<std::mutex> lk(a->mtx);
    view->result = r; view->done = true; view->name = c.name;
}

void run_benchmark(App* a) {
    a->running = true;
    int level, threads, runs, blk_kib;
    { std::lock_guard<std::mutex> lk(a->mtx);
      level = a->bench_level; threads = a->bench_threads; runs = a->runs; blk_kib = a->block_kib; }
    int accel = 10 - level; if (accel < 1) accel = 1;
    size_t blk = (size_t)blk_kib * 1024;
    const uint8_t* d = a->bench_data.data();
    size_t n = a->bench_data.size() - SZ_MARGIN;

    bool rs, rr, rt, rl, rz, rm, skip;
    { std::lock_guard<std::mutex> lk(a->mtx);
      skip = a->skip_hidden;
      rs = !skip || a->show_s; rr = !skip || a->show_r; rt = !skip || a->show_t;
      rl = !skip || a->show_lz4; rz = !skip || a->show_zip; rm = !skip || a->show_lzma; }

    Codec3 cS{ "SAT-ZIP S",
        [accel](const uint8_t* s, size_t m, uint8_t* o){ sz_state_t st; sz_state_reset(&st); return sz_compress_block(&st, s, m, o, accel); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return sz_decompress_block(s, m, o, cap); },
        [](size_t m){ return sz_compress_bound(m); } };
    Codec3 cR{ "SAT-ZIP R",
        [accel](const uint8_t* s, size_t m, uint8_t* o){ sz_state_t st; sz_state_reset(&st); return sz_compress_block_r(&st, s, m, o, accel); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return sz_decompress_block(s, m, o, cap); },
        [](size_t m){ return sz_compress_bound(m); } };
    Codec3 cT{ "SAT-ZIP T",
        [accel](const uint8_t* s, size_t m, uint8_t* o){
            std::vector<uint8_t> tok(sz_compress_bound(m) + SZ_MARGIN);
            sz_state_t st; sz_state_reset(&st);
            size_t t = sz_compress_block_r(&st, s, m, tok.data(), accel);
            if (!t) return (size_t)0;
            size_t a2 = sz_split_compress(tok.data(), t, o + 1, sz_compress_bound(m));
            if (a2 && a2 + 1 < t) { o[0] = 2; return a2 + 1; }
            o[0] = 1; memcpy(o + 1, tok.data(), t); return t + 1;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            if (!m) return (size_t)SZ_ERR;
            if (s[0] == 1) return sz_decompress_block(s + 1, m - 1, o, cap);
            std::vector<uint8_t> tok(sz_compress_bound(cap) + SZ_MARGIN);
            size_t t = sz_split_decompress(s + 1, m - 1, tok.data(), tok.size());
            if (t == SZ_ERR) return (size_t)SZ_ERR;
            return sz_decompress_block(tok.data(), t, o, cap);
        },
        [](size_t m){ return sz_compress_bound(m) + 8; } };
    if (rs) bench_parallel(a, cS, &a->sat_s, d, n, blk, runs, threads);
    if (rr) bench_parallel(a, cR, &a->sat_r, d, n, blk, runs, threads);
    if (rt) bench_parallel(a, cT, &a->sat_t, d, n, blk, runs, threads);
#if defined(HAVE_LZ4)
    Codec3 cL{ "LZ4",
        [](const uint8_t* s, size_t m, uint8_t* o){ return lz4_c(nullptr, s, m, o); },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){ return lz4_d(s, m, o, cap); },
        [](size_t m){ return lz4_bound(m); } };
    if (rl) bench_parallel(a, cL, &a->lz4, d, n, blk, runs, threads);
#endif
#if defined(SATZIP_HAVE_ZIP)
    Codec3 cZ{ "zip (deflate)",
        [level](const uint8_t* s, size_t m, uint8_t* o){
            mz_ulong dl = mz_compressBound((mz_ulong)m);
            int lv = level; if (lv < 1) lv = 1; if (lv > 9) lv = 9;
            if (mz_compress2(o, &dl, s, (mz_ulong)m, lv) != MZ_OK) return (size_t)0;
            return (size_t)dl;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            mz_ulong dl = (mz_ulong)cap;
            if (mz_uncompress(o, &dl, s, (mz_ulong)m) != MZ_OK) return (size_t)SZ_ERR;
            return (size_t)dl;
        },
        [](size_t m){ return (size_t)mz_compressBound((mz_ulong)m) + 64; } };
    if (rz) bench_parallel(a, cZ, &a->zipdef, d, n, blk, runs, threads);
#endif
#if defined(SATZIP_HAVE_7Z)
    Codec3 cM{ "LZMA",
        [level](const uint8_t* s, size_t m, uint8_t* o){
            CLzmaEncProps p; LzmaEncProps_Init(&p);
            p.level = level; p.reduceSize = m;
            Byte props[5]; SizeT ps = 5;
            SizeT dl = sz_compress_bound(m);
            SRes r = LzmaEncode(o + 5, &dl, s, m, &p, props, &ps, 0, NULL,
                                &sz::sevenz::alloc(), &sz::sevenz::alloc());
            if (r != SZ_OK) return (size_t)0;
            memcpy(o, props, 5);
            return (size_t)dl + 5;
        },
        [](const uint8_t* s, size_t m, uint8_t* o, size_t cap){
            if (m < 6) return (size_t)SZ_ERR;
            SizeT dl = cap, sl = m - 5;
            ELzmaStatus st;
            SRes r = LzmaDecode(o, &dl, s + 5, &sl, s, 5, LZMA_FINISH_ANY, &st,
                                &sz::sevenz::alloc());
            return (r == SZ_OK) ? (size_t)dl : (size_t)SZ_ERR;
        },
        [](size_t m){ return sz_compress_bound(m) + 64; } };
    if (rm) bench_parallel(a, cM, &a->lzma, d, n, blk, runs, threads);
#endif
    a->running = false;
}
void load_bench_file(App* a, const std::string& path) {
    std::error_code ec; auto sz = fs::file_size(path, ec); if (ec) return;
    auto buf = sz::read_file(path, sz, SZ_MARGIN);
    std::lock_guard<std::mutex> lk(a->mtx);
    a->bench_data = std::move(buf); a->bench_source = path;
}

void do_load_bench_folder(App* a, std::string dir) {
    a->running = true;
    { std::lock_guard<std::mutex> lk(a->mtx); a->status = "Loading folder for benchmark..."; }
    const size_t CAP = 512u * 1024 * 1024;
    std::vector<sz::Item> items;
    sz::gather(fs::path(dir), items);
    std::vector<uint8_t> buf;
    size_t files = 0;
    bool capped = false;
    for (auto& it : items) {
        if (a->cancel) break;
        if (it.is_dir || !it.size) continue;
        if (buf.size() + it.size > CAP) { capped = true; break; }
        auto data = sz::read_file(it.abs, (size_t)it.size, 0);
        buf.insert(buf.end(), data.begin(), data.end());
        ++files;
        a->prog_done = files; a->prog_total = items.size();
    }
    buf.resize(buf.size() + SZ_MARGIN, 0);
    { std::lock_guard<std::mutex> lk(a->mtx);
      a->bench_data = std::move(buf);
      char s[64]; snprintf(s, sizeof(s), " (%zu files, %.1f MB%s)", files,
                           (a->bench_data.size() - SZ_MARGIN) / 1e6, capped ? ", capped at 512MB" : "");
      a->bench_source = "folder: " + fs::path(dir).filename().string() + s; }
    a->running = false;
}

static uint64_t g_rng = 0x243F6A8885A308D3ULL;
static uint32_t xr() { uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; g_rng = x; return (uint32_t)(x >> 32); }
void gen_corpus(App* a, const char* kind) {
    const size_t CAP = 16u * 1024 * 1024;
    std::vector<uint8_t> b(CAP + SZ_MARGIN, 0);
    if (!strcmp(kind, "random")) { for (size_t i = 0; i < CAP; ++i) b[i] = (uint8_t)xr(); }
    else if (!strcmp(kind, "text")) {
        static const char* w[] = {"the ","quick ","brown ","fox ","jumps ","over ","lazy ","dog ","while ","cat "};
        size_t i = 0; while (i < CAP - 16) { const char* s = w[xr() % 10]; size_t l = strlen(s); memcpy(&b[i], s, l); i += l; }
    } else if (!strcmp(kind, "mixed")) {
        size_t i = 0;
        while (i < CAP) {
            if (xr() & 1) { uint8_t v = (uint8_t)xr(); size_t r = 1 + xr() % 80; while (r-- && i < CAP) b[i++] = v; }
            else          { size_t r = 1 + xr() % 24; while (r-- && i < CAP) b[i++] = (uint8_t)xr(); }
        }
    }
    std::lock_guard<std::mutex> lk(a->mtx);
    a->bench_data = std::move(b);
    a->bench_source = std::string("synthetic: ") + kind + " (16 MB)";
}

}

#include "satzip_archive.h"
#include "satzip_stream.h"
#include <stdlib.h>

static uint32_t g_crc_tab[256];
static int      g_crc_ready = 0;
static void crc_build(void) {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_tab[i] = c;
    }
    g_crc_ready = 1;
}
uint32_t sz_crc32(const uint8_t* data, size_t n) {
    if (!g_crc_ready) crc_build();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = g_crc_tab[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void wr16(uint8_t* p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wr32(uint8_t* p, uint32_t v){ for(int i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void wr64(uint8_t* p, uint64_t v){ for(int i=0;i<8;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd32(const uint8_t* p){ uint32_t v=0; for(int i=0;i<4;++i) v|=(uint32_t)p[i]<<(8*i); return v; }
static uint64_t rd64(const uint8_t* p){ uint64_t v=0; for(int i=0;i<8;++i) v|=(uint64_t)p[i]<<(8*i); return v; }

typedef struct {
    uint8_t  type, method;
    uint16_t name_len;
    uint32_t crc;
    uint64_t orig, stored, offset;
    char*    name;
} aw_entry;

int sz_aw_init(sz_archive_writer* w, uint8_t* out, size_t out_cap) {
    if (out_cap < SZ_AR_HEADER + SZ_AR_END) return -1;
    w->out = out; w->cap = out_cap; w->cur = SZ_AR_HEADER;
    w->ents = NULL; w->count = 0; w->ecap = 0; w->error = 0;
    out[0]='S'; out[1]='Z'; out[2]='A'; out[3]='R';
    out[4]=1; out[5]=0; out[6]=0; out[7]=0;
    return 0;
}

static aw_entry* aw_push(sz_archive_writer* w) {
    if (w->count == w->ecap) {
        uint32_t nc = w->ecap ? w->ecap * 2 : 16;
        aw_entry* ne = (aw_entry*)realloc(w->ents, nc * sizeof(aw_entry));
        if (!ne) { w->error = 1; return NULL; }
        w->ents = ne; w->ecap = nc;
    }
    return &((aw_entry*)w->ents)[w->count];
}

static aw_entry* aw_begin(sz_archive_writer* w, const char* name) {
    size_t nlen = strlen(name);
    if (nlen > 0xFFFF) { w->error = 1; return NULL; }
    aw_entry* e = aw_push(w);
    if (!e) return NULL;
    e->name = (char*)malloc(nlen + 1);
    if (!e->name) { w->error = 1; return NULL; }
    memcpy(e->name, name, nlen + 1);
    e->name_len = (uint16_t)nlen;
    e->offset   = w->cur;
    return e;
}

int sz_aw_add(sz_archive_writer* w, const char* name, int is_dir,
              const uint8_t* data, size_t size, sz_state_t* st, int acceleration, int mode) {
    if (w->error) return -1;
    aw_entry* e = aw_begin(w, name);
    if (!e) return -1;

    if (is_dir) {
        e->type = SZ_AR_DIR; e->method = 0; e->crc = 0; e->orig = 0; e->stored = 0;
    } else {
        e->type = SZ_AR_FILE;
        e->orig = size;
        e->crc  = sz_crc32(data, size);
        size_t room = w->cap - w->cur;
        size_t c = sz_stream_compress(st, data, size, w->out + w->cur, room, 16, acceleration, mode);
        if (c != 0 && c < size) {
            e->method = 1; e->stored = c;
        } else {
            if (size > room) { w->error = 1; return -1; }
            memcpy(w->out + w->cur, data, size);
            e->method = 0; e->stored = size;
        }
        w->cur += e->stored;
    }
    w->count++;
    return 0;
}

int sz_aw_add_blob(sz_archive_writer* w, const char* name, int is_dir,
                   uint8_t method, uint32_t crc, uint64_t orig,
                   const uint8_t* payload, size_t stored) {
    if (w->error) return -1;
    aw_entry* e = aw_begin(w, name);
    if (!e) return -1;
    if (is_dir) {
        e->type = SZ_AR_DIR; e->method = 0; e->crc = 0; e->orig = 0; e->stored = 0;
    } else {
        e->type = SZ_AR_FILE; e->method = method; e->crc = crc; e->orig = orig; e->stored = stored;
        if (stored > w->cap - w->cur) { w->error = 1; return -1; }
        if (stored) memcpy(w->out + w->cur, payload, stored);
        w->cur += stored;
    }
    w->count++;
    return 0;
}

size_t sz_aw_finish(sz_archive_writer* w) {
    size_t total = 0;
    if (!w->error) {
        aw_entry* ents = (aw_entry*)w->ents;
        uint64_t dir_off = w->cur;
        for (uint32_t i = 0; i < w->count && !w->error; ++i) {
            aw_entry* e = &ents[i];
            if (w->cur + SZ_AR_DIR_FIXED + e->name_len > w->cap) { w->error = 1; break; }
            uint8_t* p = w->out + w->cur;
            p[0] = e->type; p[1] = e->method;
            wr16(p + 2, e->name_len);
            wr32(p + 4, e->crc);
            wr64(p + 8,  e->orig);
            wr64(p + 16, e->stored);
            wr64(p + 24, e->offset);
            memcpy(p + SZ_AR_DIR_FIXED, e->name, e->name_len);
            w->cur += SZ_AR_DIR_FIXED + e->name_len;
        }
        if (!w->error && w->cur + SZ_AR_END <= w->cap) {
            uint8_t* p = w->out + w->cur;
            wr64(p, dir_off);
            wr32(p + 8, w->count);
            p[12]='S'; p[13]='Z'; p[14]='E'; p[15]='D';
            w->cur += SZ_AR_END;
            total = w->cur;
        } else {
            w->error = 1;
        }
    }
    if (w->ents) {
        aw_entry* ents = (aw_entry*)w->ents;
        for (uint32_t i = 0; i < w->count; ++i) free(ents[i].name);
        free(w->ents);
        w->ents = NULL;
    }
    return w->error ? 0 : total;
}

int sz_ar_open(const uint8_t* arc, size_t n, sz_ar_entry** out_entries, uint32_t* out_count) {
    if (n < SZ_AR_HEADER + SZ_AR_END) return -1;
    if (!(arc[0]=='S'&&arc[1]=='Z'&&arc[2]=='A'&&arc[3]=='R') || arc[4] != 1) return -1;

    const uint8_t* end = arc + n - SZ_AR_END;
    if (!(end[12]=='S'&&end[13]=='Z'&&end[14]=='E'&&end[15]=='D')) return -1;
    uint64_t dir_off = rd64(end);
    uint32_t count   = rd32(end + 8);
    if (dir_off > n - SZ_AR_END) return -1;

    sz_ar_entry* es = (sz_ar_entry*)calloc(count ? count : 1, sizeof(sz_ar_entry));
    if (!es) return -1;

    const uint8_t* p   = arc + dir_off;
    const uint8_t* lim = arc + n - SZ_AR_END;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + SZ_AR_DIR_FIXED > lim) { sz_ar_close(es, i); return -1; }
        uint16_t nlen = rd16(p + 2);
        if (p + SZ_AR_DIR_FIXED + nlen > lim) { sz_ar_close(es, i); return -1; }
        es[i].is_dir      = p[0] == SZ_AR_DIR;
        es[i].method      = p[1];
        es[i].crc         = rd32(p + 4);
        es[i].orig_size   = rd64(p + 8);
        es[i].stored_size = rd64(p + 16);
        es[i].offset      = rd64(p + 24);
        es[i].name = (char*)malloc((size_t)nlen + 1);
        if (!es[i].name) { sz_ar_close(es, i); return -1; }
        memcpy(es[i].name, p + SZ_AR_DIR_FIXED, nlen);
        es[i].name[nlen] = 0;
        if (!es[i].is_dir) {
            if (es[i].offset + es[i].stored_size > dir_off) { sz_ar_close(es, i + 1); return -1; }
        }
        p += SZ_AR_DIR_FIXED + nlen;
    }
    *out_entries = es; *out_count = count;
    return 0;
}

size_t sz_ar_extract(const uint8_t* arc, size_t n, const sz_ar_entry* e,
                     uint8_t* out, size_t out_cap) {
    (void)n;
    if (e->is_dir) return 0;
    if (e->orig_size > out_cap) return SZ_ERR;

    const uint8_t* payload = arc + e->offset;
    size_t produced;
    if (e->method == 0) {
        if (e->stored_size != e->orig_size) return SZ_ERR;
        memcpy(out, payload, (size_t)e->orig_size);
        produced = (size_t)e->orig_size;
    } else {
        produced = sz_stream_decompress(payload, (size_t)e->stored_size, out, out_cap);
        if (produced != e->orig_size) return SZ_ERR;
    }
    if (sz_crc32(out, produced) != e->crc) return SZ_ERR;
    return produced;
}

void sz_ar_close(sz_ar_entry* entries, uint32_t count) {
    if (!entries) return;
    for (uint32_t i = 0; i < count; ++i) free(entries[i].name);
    free(entries);
}

#ifndef SATZIP_ARCHIVE_H
#define SATZIP_ARCHIVE_H

#include "satzip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SZ_AR_HEADER       8
#define SZ_AR_DIR_FIXED    32
#define SZ_AR_END          16
#define SZ_AR_FILE         0
#define SZ_AR_DIR          1

uint32_t sz_crc32(const uint8_t* data, size_t n);

typedef struct {
    uint8_t* out;
    size_t   cap, cur;
    void*    ents;
    uint32_t count, ecap;
    int      error;
} sz_archive_writer;

int    sz_aw_init(sz_archive_writer* w, uint8_t* out, size_t out_cap);
int    sz_aw_add(sz_archive_writer* w, const char* name, int is_dir,
                 const uint8_t* data, size_t size, sz_state_t* st, int acceleration, int mode);
int    sz_aw_add_blob(sz_archive_writer* w, const char* name, int is_dir,
                      uint8_t method, uint32_t crc, uint64_t orig,
                      const uint8_t* payload, size_t stored);
size_t sz_aw_finish(sz_archive_writer* w);

typedef struct {
    char*    name;
    int      is_dir;
    uint8_t  method;
    uint32_t crc;
    uint64_t orig_size, stored_size, offset;
} sz_ar_entry;

int    sz_ar_open(const uint8_t* arc, size_t n, sz_ar_entry** entries, uint32_t* count);
size_t sz_ar_extract(const uint8_t* arc, size_t n, const sz_ar_entry* e,
                     uint8_t* out, size_t out_cap);
void   sz_ar_close(sz_ar_entry* entries, uint32_t count);

#ifdef __cplusplus
}
#endif
#endif

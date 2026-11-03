/* Page cache in front of a gmap_read_fn: on an SD card each random read costs milliseconds whatever its size, and
 * nodes close on the map are close in the file (Z-order), so whole pages are read and kept.
 *
 * Set-associative (8 ways) with LRU replacement inside each set. The memory is yours: about page_size + 8 bytes per
 * page (gmap_cache_memory). On the ESP32-S3 navigator: 512 pages of 4 KiB in PSRAM.
 *
 *   static uint8_t mem[...];
 *   gmap_cache_t cache;
 *   gmap_cache_init(&cache, sd_read, &sd, file_size, mem, sizeof(mem), 4096);
 *   gmap_open(&map, gmap_cache_read, &cache, file_size);
 */
#ifndef GMAP_CACHE_H
#define GMAP_CACHE_H

#include "gmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  gmap_read_fn read;
  void *ctx;
  uint32_t file_size;
  uint32_t page_size, page_shift;
  uint32_t sets, ways;
  uint8_t *pages;   /* sets * ways pages */
  uint32_t *tags;   /* page number in each slot, or GMAP_NONE */
  uint32_t *stamps; /* last use, for LRU */
  uint32_t clock;
  uint32_t hits, misses; /* page lookups; misses are reads from the underlying device */
} gmap_cache_t;

/* Bytes of memory for `pages` pages of `page_size` bytes. */
size_t gmap_cache_memory(uint32_t pages, uint32_t page_size);

/* `page_size` must be a power of two. Uses as many pages as fit in `memory` (rounded down to whole sets).
 * Returns 0 on success, -1 if the memory holds less than one set. */
int gmap_cache_init(gmap_cache_t *c, gmap_read_fn read, void *ctx, uint32_t file_size, void *memory, size_t size,
                    uint32_t page_size);

/* Forgets every page (to measure with a cold cache, or after the file changed). */
void gmap_cache_clear(gmap_cache_t *c);

/* A gmap_read_fn: pass the cache as `ctx`. */
int gmap_cache_read(void *cache, uint32_t offset, void *dst, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif

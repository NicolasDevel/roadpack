#include "gmap_cache.h"

#include <string.h>

#define WAYS 8

size_t gmap_cache_memory(uint32_t pages, uint32_t page_size) {
  return (size_t)pages * (page_size + 2 * sizeof(uint32_t)) + sizeof(uint32_t);  /* + alignment slack */
}

int gmap_cache_init(gmap_cache_t *c, gmap_read_fn read, void *ctx, uint32_t file_size, void *memory, size_t size,
                    uint32_t page_size) {
  uint32_t shift = 0, pages;
  uint8_t *p = (uint8_t *)memory;
  memset(c, 0, sizeof(*c));
  while ((1u << shift) < page_size) shift++;
  if (page_size == 0 || (1u << shift) != page_size) return -1;
  pages = (uint32_t)((size - sizeof(uint32_t)) / (page_size + 2 * sizeof(uint32_t)));
  if (pages < WAYS) return -1;
  c->read = read;
  c->ctx = ctx;
  c->file_size = file_size;
  c->page_size = page_size;
  c->page_shift = shift;
  c->ways = WAYS;
  c->sets = pages / WAYS;
  pages = c->sets * WAYS;
  /* Tags and stamps first (4-byte aligned), then the pages. */
  while ((uintptr_t)p & 3) p++;
  c->tags = (uint32_t *)p;
  c->stamps = c->tags + pages;
  c->pages = (uint8_t *)(c->stamps + pages);
  gmap_cache_clear(c);
  return 0;
}

void gmap_cache_clear(gmap_cache_t *c) {
  uint32_t i;
  for (i = 0; i < c->sets * c->ways; i++) c->tags[i] = GMAP_NONE, c->stamps[i] = 0;
  c->clock = 0;
}

/* Slot holding `page`, reading it on a miss. NULL if the underlying read failed. */
static const uint8_t *page_data(gmap_cache_t *c, uint32_t page) {
  const uint32_t set = page % c->sets, first = set * c->ways;
  uint32_t i, victim = first;
  for (i = first; i < first + c->ways; i++) {
    if (c->tags[i] == page) {
      c->hits++;
      c->stamps[i] = ++c->clock;
      return c->pages + (size_t)i * c->page_size;
    }
    if (c->stamps[i] < c->stamps[victim]) victim = i;
  }
  {
    const uint32_t start = page << c->page_shift;
    uint32_t len = c->page_size;
    uint8_t *dst = c->pages + (size_t)victim * c->page_size;
    if (c->file_size && start + len > c->file_size) len = c->file_size - start;  /* last page */
    c->misses++;
    c->tags[victim] = GMAP_NONE;
    if (c->read(c->ctx, start, dst, len) != 0) return NULL;
    c->tags[victim] = page;
    c->stamps[victim] = ++c->clock;
    return dst;
  }
}

int gmap_cache_read(void *cache, uint32_t offset, void *dst, uint32_t len) {
  gmap_cache_t *c = (gmap_cache_t *)cache;
  uint8_t *out = (uint8_t *)dst;
  if (c->file_size && (offset > c->file_size || len > c->file_size - offset)) return -1;
  while (len > 0) {
    const uint32_t page = offset >> c->page_shift, in_page = offset & (c->page_size - 1);
    const uint32_t n = len < c->page_size - in_page ? len : c->page_size - in_page;
    const uint8_t *src = page_data(c, page);
    if (!src) return -1;
    memcpy(out, src + in_page, n);
    out += n, offset += n, len -= n;
  }
  return 0;
}

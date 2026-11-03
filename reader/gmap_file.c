#include "gmap_file.h"

#include <string.h>

static long file_size(FILE *f) {
  long n;
  if (fseek(f, 0, SEEK_END) != 0) return -1;
  n = ftell(f);
  return fseek(f, 0, SEEK_SET) == 0 ? n : -1;
}

int gmap_file_open(gmap_file_t *f, const char *path) {
  char name[1024];
  memset(f, 0, sizeof(*f));
  /* A directory of chunks if it has 0000.bin; otherwise the path itself is the file. */
  for (;;) {
    FILE *c;
    long n;
    if (strlen(path) + 10 >= sizeof(name)) break;
    snprintf(name, sizeof(name), "%s/%04u.bin", path, (unsigned)f->count);
    c = fopen(name, "rb");
    if (!c) break;
    n = file_size(c);
    if (n <= 0 || f->count == GMAP_FILE_MAX_CHUNKS) {
      fclose(c);
      gmap_file_close(f);
      return -1;
    }
    if (f->count == 0) f->chunk_size = (uint32_t)n;
    f->chunks[f->count++] = c;
    f->size += (uint32_t)n;
  }
  if (f->count == 0) {
    long n;
    FILE *c = fopen(path, "rb");
    if (!c) return -1;
    n = file_size(c);
    if (n <= 0) {
      fclose(c);
      return -1;
    }
    f->chunks[0] = c;
    f->count = 1;
    f->size = f->chunk_size = (uint32_t)n;
  }
  return 0;
}

void gmap_file_close(gmap_file_t *f) {
  uint32_t i;
  for (i = 0; i < f->count; i++) fclose(f->chunks[i]);
  f->count = 0;
}

int gmap_file_read(void *file, uint32_t offset, void *dst, uint32_t len) {
  gmap_file_t *f = (gmap_file_t *)file;
  uint8_t *out = (uint8_t *)dst;
  f->reads++;
  if (offset > f->size || len > f->size - offset) return -1;
  while (len > 0) {
    const uint32_t k = offset / f->chunk_size, in_chunk = offset % f->chunk_size;
    const uint32_t n = len < f->chunk_size - in_chunk ? len : f->chunk_size - in_chunk;
    if (k >= f->count || fseek(f->chunks[k], (long)in_chunk, SEEK_SET) != 0 || fread(out, 1, n, f->chunks[k]) != n)
      return -1;
    out += n, offset += n, len -= n;
  }
  return 0;
}

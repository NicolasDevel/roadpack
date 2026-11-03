/* Reading a .gmap from a file system with stdio (desktop, or a microcontroller with a POSIX-like FS): a single .gmap
 * file, or a directory of the SD card chunks that `roadpack build` writes (0000.bin, 0001.bin, ...), which are one
 * continuous file split in pieces of the same size. Keeps one FILE open per chunk.
 *
 *   gmap_file_t file;
 *   if (gmap_file_open(&file, "out/colombia") == 0)
 *     gmap_open(&map, gmap_file_read, &file, file.size);
 */
#ifndef GMAP_FILE_H
#define GMAP_FILE_H

#include <stdio.h>

#include "gmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GMAP_FILE_MAX_CHUNKS 1024

typedef struct {
  FILE *chunks[GMAP_FILE_MAX_CHUNKS];
  uint32_t count;      /* 1 for a plain file */
  uint32_t chunk_size; /* size of every chunk but the last */
  uint32_t size;       /* total bytes */
  uint32_t reads;      /* calls to gmap_file_read */
} gmap_file_t;

/* Opens `path`: a .gmap file, or a directory with 0000.bin, 0001.bin, ... Returns 0, or -1. */
int gmap_file_open(gmap_file_t *f, const char *path);
void gmap_file_close(gmap_file_t *f);
/* A gmap_read_fn: pass the gmap_file_t as `ctx`. */
int gmap_file_read(void *file, uint32_t offset, void *dst, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif

/* Reader for .gmap files (versions 2 and 3, see FORMAT.md), in portable C99 for microcontrollers.
 *
 * The map is never loaded: every access reads a few bytes through a function you provide (an SD card, a file, a
 * memory-mapped flash region...), optionally through a page cache (gmap_cache.h). Nothing here allocates memory.
 * Records are decoded byte by byte, so the host's endianness and alignment do not matter.
 *
 *   gmap_t map;
 *   if (gmap_open(&map, my_read, my_ctx) != GMAP_OK) ...
 *   uint32_t n = gmap_nearest_node(&map, 4.6097, -74.0817);
 */
#ifndef GMAP_H
#define GMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GMAP_NONE 0xFFFFFFFFu   /* "nothing here" in 32-bit references */
#define GMAP_SCALE 10000000.0   /* coordinates are degrees x 1e7 */

/* Edge flags: road class in the low 4 bits (0 motorway ... 7 service), then these. */
#define GMAP_CLASS_MASK 0x0F
#define GMAP_FLAG_GEOM_REVERSED 0x10
#define GMAP_FLAG_ROUNDABOUT 0x20
#define GMAP_FLAG_LINK 0x40

typedef enum {
  GMAP_OK = 0,
  GMAP_ERR_IO,        /* the read function failed */
  GMAP_ERR_FORMAT,    /* not a .gmap, or an unsupported version */
  GMAP_ERR_TRUNCATED, /* the size in the header does not match (when the size is known) */
} gmap_status;

/* Reads `len` bytes at absolute offset `offset` of the file into `dst`. Returns 0 on success. */
typedef int (*gmap_read_fn)(void *ctx, uint32_t offset, void *dst, uint32_t len);

typedef struct {
  int32_t lat, lon;  /* degrees x 1e7 */
} gmap_point;

typedef struct {
  uint32_t source, target;
  uint32_t geom;      /* offset in the geometry section, or GMAP_NONE */
  uint32_t name;      /* offset in the names section, or GMAP_NONE */
  uint32_t length_m;
  uint32_t weight_ds; /* travel time in tenths of a second: the routing weight */
  uint8_t speed_kmh;
  uint8_t flags;
} gmap_edge;

typedef struct {
  uint32_t other;  /* forward: higher-ranked head; backward: higher-ranked tail */
  uint32_t weight_ds;
  uint32_t a, b;   /* original arc: a = 0x80000000 | edge id; shortcut: backward arc a, forward arc b */
} gmap_ch_arc;

typedef struct {
  gmap_read_fn read;
  void *ctx;
  uint16_t version;
  uint32_t node_count, edge_count;
  int32_t min_lat, min_lon, max_lat, max_lon;
  uint32_t cell_size;
  uint16_t grid_cols, grid_rows;
  uint32_t nodes_off, edges_off, grid_off, geom_off, names_off, ch_fwd_off, ch_bwd_off, file_size;
  uint32_t copies_off, copy_count; /* junction copies (v3); 0 in v2 */
  uint32_t cell_count;             /* non-empty cells of the spatial grid */
  int io_error;                    /* set when a read failed: results since then are not trustworthy */
} gmap_t;

/* Reads and checks the header. `file_size` is the size of the file if known (to detect truncation), or 0. */
gmap_status gmap_open(gmap_t *m, gmap_read_fn read, void *ctx, uint32_t file_size);

/* Nodes and edges. Out-of-range ids, or a failed read, give zeros (and set m->io_error on a failed read). */
gmap_point gmap_node_point(gmap_t *m, uint32_t node);
uint32_t gmap_first_edge(gmap_t *m, uint32_t node); /* edges of `node` are [first_edge(node), first_edge(node + 1)) */
gmap_edge gmap_get_edge(gmap_t *m, uint32_t edge);
gmap_ch_arc gmap_get_ch_arc(gmap_t *m, int forward, uint32_t arc);
uint32_t gmap_ch_first(gmap_t *m, int forward, uint32_t node); /* arcs of `node` are [ch_first(node), ch_first(node + 1)) */

/* Junction copies made by turn restrictions (v3). The original of a node that is not a copy is the node itself. */
uint32_t gmap_original(gmap_t *m, uint32_t node);
/* Copies of a junction (they follow it in node order): writes up to `max` of them and returns how many there are. */
uint32_t gmap_copies_of(gmap_t *m, uint32_t node, uint32_t *out, uint32_t max);

/* Points of an edge from source to target, both ends included. Writes up to `max` and returns how many there are. */
uint32_t gmap_polyline(gmap_t *m, uint32_t edge, gmap_point *out, uint32_t max);
/* Name of an edge (UTF-8, OSM `name` or `ref`), copied into `out` (always NUL-terminated); "" if it has none. */
size_t gmap_edge_name(gmap_t *m, uint32_t edge, char *out, size_t size);
size_t gmap_name_at(gmap_t *m, uint32_t name_ref, char *out, size_t size);

/* Spatial grid. */
void gmap_cell_of(const gmap_t *m, double lat, double lon, int32_t *row, int32_t *col);
/* Edges listed in a cell (one direction of each segment touching it): writes up to `max`, returns how many. */
uint32_t gmap_edges_in_cell(gmap_t *m, int32_t row, int32_t col, uint32_t *out, uint32_t max);
/* Closest junction (an original node) to a position, searching up to `radius` cells around it (4 is ~1 km).
 * `scratch` holds edge ids while searching (`scratch_len` entries; 256 is plenty for a city). GMAP_NONE if none. */
uint32_t gmap_nearest_node(gmap_t *m, double lat, double lon, int radius, uint32_t *scratch, uint32_t scratch_len);

/* Meters from a to b, with a flat approximation around a (fine at street scale; same as roadpack.geo.local_xy). */
double gmap_distance_m(gmap_point a, gmap_point b);

#ifdef __cplusplus
}
#endif
#endif

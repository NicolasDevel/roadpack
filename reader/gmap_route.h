/* Route queries on a .gmap with its contraction hierarchy (see FORMAT.md, "Contraction hierarchy").
 *
 * Bidirectional Dijkstra going only up in the hierarchy, then the shortcuts are unpacked into original edges. A query
 * settles a few hundred nodes even across a country, so instead of per-node arrays it uses two small hash tables
 * and two heaps, in memory you provide (gmap_router_memory). Nothing is allocated.
 *
 *   static uint8_t mem[...];  // gmap_router_memory(1 << 14, 1 << 14, 512): about 770 KB
 *   gmap_router_t router;
 *   gmap_router_init(&router, mem, sizeof(mem), 1 << 14, 1 << 14, 512);
 *   uint32_t path[4096], len, cost;
 *   if (gmap_route(&router, &map, start, goal, path, 4096, &len, &cost) == GMAP_ROUTE_OK) ...
 *
 * With turn restrictions (.gmap v3) the route may end at a copy of the goal: arriving along a restricted way is
 * allowed (gmap_original() of the last edge's target is the goal).
 */
#ifndef GMAP_ROUTE_H
#define GMAP_ROUTE_H

#include "gmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GMAP_ROUTE_OK = 0,
  GMAP_ROUTE_NO_PATH,      /* the goal cannot be reached from the start */
  GMAP_ROUTE_FULL,         /* the hash tables, heaps or unpacking stack were too small */
  GMAP_ROUTE_PATH_TOO_LONG,/* the route has more edges than `path_cap`; *path_len says how many */
  GMAP_ROUTE_IO,           /* a read failed: the result would not be trustworthy */
} gmap_route_status;

typedef struct {
  uint32_t node, dist, pred_arc, pred_node;
} gmap_route_entry;

typedef struct {
  uint32_t dist, node;
} gmap_heap_item;

typedef struct {
  gmap_route_entry *table[2]; /* forward and backward searches */
  uint32_t table_size, table_used[2];
  gmap_heap_item *heap[2];
  uint32_t heap_cap, heap_len[2];
  uint32_t *stack;            /* arcs pending to unpack */
  uint32_t stack_cap;
  /* Last query. */
  uint32_t settled;
  uint32_t start_used;        /* index in `starts` the route left from (gmap_route_multi) */
} gmap_router_t;

/* Bytes needed (2 x (16 x table_size + 8 x heap_cap) + 4 x stack_cap). `table_size` (a power of two)
 * bounds the nodes reached per side: even across a country a query reaches a few thousand (1 << 12 routed 300
 * random pairs of Colombia; 1 << 14 leaves margin). `heap_cap` bounds the pending queue per side; `stack_cap` the
 * nesting of shortcuts while unpacking (a few hundred is enough). If they are too small the query fails with
 * GMAP_ROUTE_FULL: never a wrong route. */
size_t gmap_router_memory(uint32_t table_size, uint32_t heap_cap, uint32_t stack_cap);
/* Returns 0, or -1 if `size` is too small or `table_size` is not a power of two. */
int gmap_router_init(gmap_router_t *r, void *memory, size_t size, uint32_t table_size, uint32_t heap_cap,
                     uint32_t stack_cap);

/* Fastest route from `start` to `goal` (or one of its copies) as edge ids. Cost in tenths of a second. A route from
 * a node to itself is empty. */
gmap_route_status gmap_route(gmap_router_t *r, gmap_t *m, uint32_t start, uint32_t goal, uint32_t *path,
                             uint32_t path_cap, uint32_t *path_len, uint32_t *cost);

/* The same with several possible starts, each with a starting cost (tenths of a second): for example both ends of
 * the road a vehicle is on, with the time to reach each. r->start_used tells which one the route left from. */
gmap_route_status gmap_route_multi(gmap_router_t *r, gmap_t *m, const uint32_t *starts, const uint32_t *offsets,
                                   uint32_t start_count, uint32_t goal, uint32_t *path, uint32_t path_cap,
                                   uint32_t *path_len, uint32_t *cost);

#ifdef __cplusplus
}
#endif
#endif

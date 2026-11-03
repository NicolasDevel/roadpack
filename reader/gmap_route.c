#include "gmap_route.h"

#include <string.h>

#define INF 0xFFFFFFFFu
#define CH_ORIGINAL 0x80000000u
#define FORWARD_BIT 0x80000000u  /* in the unpacking stack: the arc is a forward one */

size_t gmap_router_memory(uint32_t table_size, uint32_t heap_cap, uint32_t stack_cap) {
  return 2 * (size_t)table_size * sizeof(gmap_route_entry) + 2 * (size_t)heap_cap * sizeof(gmap_heap_item) +
         (size_t)stack_cap * sizeof(uint32_t) + 8;
}

int gmap_router_init(gmap_router_t *r, void *memory, size_t size, uint32_t table_size, uint32_t heap_cap,
                     uint32_t stack_cap) {
  uint8_t *p = (uint8_t *)memory;
  memset(r, 0, sizeof(*r));
  if (table_size == 0 || (table_size & (table_size - 1)) || size < gmap_router_memory(table_size, heap_cap, stack_cap))
    return -1;
  while ((uintptr_t)p & 7) p++;
  r->table[0] = (gmap_route_entry *)p;
  r->table[1] = r->table[0] + table_size;
  r->heap[0] = (gmap_heap_item *)(r->table[1] + table_size);
  r->heap[1] = r->heap[0] + heap_cap;
  r->stack = (uint32_t *)(r->heap[1] + heap_cap);
  r->table_size = table_size;
  r->heap_cap = heap_cap;
  r->stack_cap = stack_cap;
  return 0;
}

/* --- Hash table per side: node -> distance and predecessor (open addressing, linear probing). --- */

static gmap_route_entry *find(gmap_router_t *r, int side, uint32_t node, int insert, int *full) {
  gmap_route_entry *t = r->table[side];
  const uint32_t mask = r->table_size - 1;
  uint32_t i = (node * 2654435761u) & mask;
  for (;;) {
    if (t[i].node == node) return &t[i];
    if (t[i].node == INF) {
      if (!insert) return NULL;
      if (r->table_used[side] + 1 > r->table_size - r->table_size / 4) {  /* keep it at most 3/4 full */
        *full = 1;
        return NULL;
      }
      r->table_used[side]++;
      t[i].node = node;
      t[i].dist = INF;
      t[i].pred_arc = INF;
      t[i].pred_node = INF;
      return &t[i];
    }
    i = (i + 1) & mask;
  }
}

/* --- Binary min-heap per side, with lazy deletion (stale items are skipped when popped). --- */

static int heap_push(gmap_router_t *r, int side, uint32_t dist, uint32_t node) {
  gmap_heap_item *h = r->heap[side];
  uint32_t i = r->heap_len[side];
  if (i == r->heap_cap) return 0;
  r->heap_len[side]++;
  while (i > 0) {
    const uint32_t parent = (i - 1) / 2;
    if (h[parent].dist <= dist) break;
    h[i] = h[parent];
    i = parent;
  }
  h[i].dist = dist;
  h[i].node = node;
  return 1;
}

static gmap_heap_item heap_pop(gmap_router_t *r, int side) {
  gmap_heap_item *h = r->heap[side];
  const gmap_heap_item top = h[0], last = h[--r->heap_len[side]];
  const uint32_t n = r->heap_len[side];
  uint32_t i = 0;
  for (;;) {
    uint32_t child = 2 * i + 1;
    if (child >= n) break;
    if (child + 1 < n && h[child + 1].dist < h[child].dist) child++;
    if (last.dist <= h[child].dist) break;
    h[i] = h[child];
    i = child;
  }
  if (n > 0) h[i] = last;
  return top;
}

/* --- Unpacking: a shortcut is backward arc `a` followed by forward arc `b`, down to original edges. --- */

/* Appends the original edges of `arc` to path[*len...]. With `reversed`, appends them in reverse order. */
static gmap_route_status unpack(gmap_router_t *r, gmap_t *m, int forward, uint32_t arc, int reversed, uint32_t *path,
                                uint32_t cap, uint32_t *len) {
  uint32_t top = 0;
  r->stack[top++] = arc | (forward ? FORWARD_BIT : 0);
  while (top > 0) {
    const uint32_t item = r->stack[--top];
    const gmap_ch_arc ch = gmap_get_ch_arc(m, (item & FORWARD_BIT) != 0, item & ~FORWARD_BIT);
    if (ch.a & CH_ORIGINAL) {
      if (*len < cap) path[*len] = ch.a & ~CH_ORIGINAL;
      (*len)++;
      continue;
    }
    if (top + 2 > r->stack_cap) return GMAP_ROUTE_FULL;
    /* In order: process a (backward) then b (forward), so b is pushed first. Reversed: the other way round. */
    if (!reversed) {
      r->stack[top++] = ch.b | FORWARD_BIT;
      r->stack[top++] = ch.a;
    } else {
      r->stack[top++] = ch.a;
      r->stack[top++] = ch.b | FORWARD_BIT;
    }
  }
  return GMAP_ROUTE_OK;
}

gmap_route_status gmap_route_multi(gmap_router_t *r, gmap_t *m, const uint32_t *starts, const uint32_t *offsets,
                                   uint32_t start_count, uint32_t goal, uint32_t *path, uint32_t path_cap,
                                   uint32_t *path_len, uint32_t *cost) {
  uint32_t best = INF, meet = INF, i, n, goals[16 + 1];
  int full = 0, side;
  gmap_route_entry *e;
  gmap_route_status st;

  *path_len = 0;
  *cost = INF;
  m->io_error = 0;  /* only this query's reads count */
  r->settled = 0;
  r->start_used = 0;
  for (side = 0; side < 2; side++) {
    memset(r->table[side], 0xFF, (size_t)r->table_size * sizeof(gmap_route_entry));  /* node = INF: empty */
    r->table_used[side] = 0;
    r->heap_len[side] = 0;
  }

  /* Forward search from the starts, each with its starting cost. */
  for (i = 0; i < start_count; i++) {
    e = find(r, 0, starts[i], 1, &full);
    if (!e) return GMAP_ROUTE_FULL;
    if (offsets[i] < e->dist) {
      e->dist = offsets[i];
      if (!heap_push(r, 0, offsets[i], starts[i])) return GMAP_ROUTE_FULL;
    }
  }
  /* Backward search from the goal and its copies: the route may arrive at the junction along any of its ways. */
  n = gmap_copies_of(m, goal, goals, 16);
  if (n > 16) n = 16;  /* never seen more than a handful */
  goals[n++] = goal;
  for (i = 0; i < n; i++) {
    e = find(r, 1, goals[i], 1, &full);
    if (!e) return GMAP_ROUTE_FULL;
    e->dist = 0;
    if (!heap_push(r, 1, 0, goals[i])) return GMAP_ROUTE_FULL;
  }

  while (r->heap_len[0] > 0 || r->heap_len[1] > 0) {
    /* Advance the side with the lowest key; each side stops once it cannot improve the best route. */
    const int k = r->heap_len[0] > 0 && (r->heap_len[1] == 0 || r->heap[0][0].dist <= r->heap[1][0].dist) ? 0 : 1;
    const gmap_heap_item item = heap_pop(r, k);
    gmap_route_entry *other;
    uint32_t a, first, end;
    if (item.dist >= best) {
      r->heap_len[k] = 0;
      continue;
    }
    e = find(r, k, item.node, 0, &full);
    if (!e || item.dist > e->dist) continue;  /* stale */
    r->settled++;
    other = find(r, 1 - k, item.node, 0, &full);
    if (other && other->dist != INF && item.dist + other->dist < best) best = item.dist + other->dist, meet = item.node;
    first = gmap_ch_first(m, k == 0, item.node);
    end = gmap_ch_first(m, k == 0, item.node + 1);
    for (a = first; a < end; a++) {
      const gmap_ch_arc arc = gmap_get_ch_arc(m, k == 0, a);
      const uint32_t nd = item.dist + arc.weight_ds;
      gmap_route_entry *next = find(r, k, arc.other, 1, &full);
      if (!next) return GMAP_ROUTE_FULL;
      if (nd < next->dist) {
        next->dist = nd;
        next->pred_arc = a;
        next->pred_node = item.node;
        if (!heap_push(r, k, nd, arc.other)) return GMAP_ROUTE_FULL;
      }
    }
  }
  if (m->io_error) return GMAP_ROUTE_IO;
  if (meet == INF) return GMAP_ROUTE_NO_PATH;
  *cost = best;

  /* Up from the start: walking back from the meeting node gives the arcs in reverse, so each one is unpacked in
   * reverse and the whole stretch is flipped at the end. Then down to the goal, in order. */
  for (e = find(r, 0, meet, 0, &full); e->pred_node != INF; e = find(r, 0, e->pred_node, 0, &full)) {
    st = unpack(r, m, 1, e->pred_arc, 1, path, path_cap, path_len);
    if (st != GMAP_ROUTE_OK) return st;
  }
  for (i = 0; i < start_count; i++) {  /* the start whose starting cost won (a node may be given twice) */
    if (starts[i] == e->node && offsets[i] == e->dist) {
      r->start_used = i;
      break;
    }
  }
  {
    const uint32_t up = *path_len < path_cap ? *path_len : path_cap;
    for (i = 0; i < up / 2; i++) {
      const uint32_t t = path[i];
      path[i] = path[up - 1 - i];
      path[up - 1 - i] = t;
    }
  }
  for (e = find(r, 1, meet, 0, &full); e->pred_node != INF; e = find(r, 1, e->pred_node, 0, &full)) {
    st = unpack(r, m, 0, e->pred_arc, 0, path, path_cap, path_len);
    if (st != GMAP_ROUTE_OK) return st;
  }
  if (m->io_error) return GMAP_ROUTE_IO;
  return *path_len > path_cap ? GMAP_ROUTE_PATH_TOO_LONG : GMAP_ROUTE_OK;
}

gmap_route_status gmap_route(gmap_router_t *r, gmap_t *m, uint32_t start, uint32_t goal, uint32_t *path,
                             uint32_t path_cap, uint32_t *path_len, uint32_t *cost) {
  const uint32_t offset = 0;
  return gmap_route_multi(r, m, &start, &offset, 1, goal, path, path_cap, path_len, cost);
}

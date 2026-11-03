#include "gmap.h"

#include <math.h>
#include <string.h>

#define HEADER_V2 72
#define HEADER_V3 80
#define NODE_SIZE 12
#define EDGE_SIZE 28
#define ARC_SIZE 16
#define COPY_SIZE 8

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Reads into `dst`; on failure zero-fills it and remembers the error. */
static int rd(gmap_t *m, uint32_t off, void *dst, uint32_t len) {
  if (m->read(m->ctx, off, dst, len) == 0) return 1;
  memset(dst, 0, len);
  m->io_error = 1;
  return 0;
}

static uint32_t rd32(gmap_t *m, uint32_t off) {
  uint8_t b[4];
  rd(m, off, b, 4);
  return le32(b);
}

gmap_status gmap_open(gmap_t *m, gmap_read_fn read, void *ctx, uint32_t file_size) {
  uint8_t h[HEADER_V3];
  memset(m, 0, sizeof(*m));
  m->read = read;
  m->ctx = ctx;
  if (read(ctx, 0, h, HEADER_V2) != 0) return GMAP_ERR_IO;
  if (memcmp(h, "GMAP", 4) != 0) return GMAP_ERR_FORMAT;
  m->version = le16(h + 4);
  if (m->version != 2 && m->version != 3) return GMAP_ERR_FORMAT;
  m->node_count = le32(h + 8);
  m->edge_count = le32(h + 12);
  m->min_lat = (int32_t)le32(h + 16);
  m->min_lon = (int32_t)le32(h + 20);
  m->max_lat = (int32_t)le32(h + 24);
  m->max_lon = (int32_t)le32(h + 28);
  m->cell_size = le32(h + 32);
  m->grid_cols = le16(h + 36);
  m->grid_rows = le16(h + 38);
  m->nodes_off = le32(h + 40);
  m->edges_off = le32(h + 44);
  m->grid_off = le32(h + 48);
  m->geom_off = le32(h + 52);
  m->names_off = le32(h + 56);
  m->ch_fwd_off = le32(h + 60);
  m->ch_bwd_off = le32(h + 64);
  m->file_size = le32(h + 68);
  if (m->version >= 3) {
    if (read(ctx, HEADER_V2, h + HEADER_V2, HEADER_V3 - HEADER_V2) != 0) return GMAP_ERR_IO;
    m->copies_off = le32(h + 72);
    m->copy_count = le32(h + 76);
  } else {
    m->copies_off = m->file_size;
  }
  if (file_size && file_size != m->file_size) return GMAP_ERR_TRUNCATED;
  m->cell_count = rd32(m, m->grid_off);
  return m->io_error ? GMAP_ERR_IO : GMAP_OK;
}

gmap_point gmap_node_point(gmap_t *m, uint32_t node) {
  uint8_t b[8];
  gmap_point p = {0, 0};
  if (node >= m->node_count) return p;
  rd(m, m->nodes_off + node * NODE_SIZE, b, 8);
  p.lat = (int32_t)le32(b);
  p.lon = (int32_t)le32(b + 4);
  return p;
}

uint32_t gmap_first_edge(gmap_t *m, uint32_t node) {
  if (node > m->node_count) return m->edge_count;  /* node_count is the sentinel record */
  return rd32(m, m->nodes_off + node * NODE_SIZE + 8);
}

gmap_edge gmap_get_edge(gmap_t *m, uint32_t edge) {
  uint8_t b[EDGE_SIZE];
  gmap_edge e;
  memset(&e, 0, sizeof(e));
  if (edge >= m->edge_count) return e;
  rd(m, m->edges_off + edge * EDGE_SIZE, b, EDGE_SIZE);
  e.source = le32(b);
  e.target = le32(b + 4);
  e.geom = le32(b + 8);
  e.name = le32(b + 12);
  e.length_m = le32(b + 16);
  e.weight_ds = le32(b + 20);
  e.speed_kmh = b[24];
  e.flags = b[25];
  return e;
}

uint32_t gmap_ch_first(gmap_t *m, int forward, uint32_t node) {
  return rd32(m, (forward ? m->ch_fwd_off : m->ch_bwd_off) + 4 + node * 4);
}

gmap_ch_arc gmap_get_ch_arc(gmap_t *m, int forward, uint32_t arc) {
  uint8_t b[ARC_SIZE];
  gmap_ch_arc a;
  const uint32_t base = (forward ? m->ch_fwd_off : m->ch_bwd_off) + 4 + (m->node_count + 1) * 4;
  rd(m, base + arc * ARC_SIZE, b, ARC_SIZE);
  a.other = le32(b);
  a.weight_ds = le32(b + 4);
  a.a = le32(b + 8);
  a.b = le32(b + 12);
  return a;
}

/* Index of the copies-table record of `copy`, or -1. The table is sorted by copy. */
static int64_t copy_index(gmap_t *m, uint32_t copy) {
  uint32_t lo = 0, hi = m->copy_count;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (rd32(m, m->copies_off + mid * COPY_SIZE) < copy) lo = mid + 1;
    else hi = mid;
  }
  if (lo < m->copy_count && rd32(m, m->copies_off + lo * COPY_SIZE) == copy) return lo;
  return -1;
}

uint32_t gmap_original(gmap_t *m, uint32_t node) {
  int64_t k;
  if (m->copy_count == 0) return node;
  k = copy_index(m, node);
  return k < 0 ? node : rd32(m, m->copies_off + (uint32_t)k * COPY_SIZE + 4);
}

uint32_t gmap_copies_of(gmap_t *m, uint32_t node, uint32_t *out, uint32_t max) {
  uint32_t n = 0;
  int64_t k;
  if (m->copy_count == 0) return 0;
  k = copy_index(m, node + 1);  /* copies follow their original */
  for (; k >= 0 && (uint32_t)k < m->copy_count; k++) {
    uint8_t b[COPY_SIZE];
    rd(m, m->copies_off + (uint32_t)k * COPY_SIZE, b, COPY_SIZE);
    if (le32(b + 4) != node || le32(b) != node + 1 + n) break;
    if (n < max) out[n] = le32(b);
    n++;
  }
  return n;
}

uint32_t gmap_polyline(gmap_t *m, uint32_t edge, gmap_point *out, uint32_t max) {
  const gmap_edge e = gmap_get_edge(m, edge);
  uint32_t count = 0, n = 0, i;
  if (e.geom != GMAP_NONE) {
    uint8_t h[4];
    rd(m, m->geom_off + e.geom, h, 4);
    count = le16(h);
  }
  if (n < max) out[n] = gmap_node_point(m, e.source);
  n++;
  for (i = 0; i < count; i++, n++) {
    /* Stored from the segment's start; GEOM_REVERSED edges walk it backwards. */
    const uint32_t k = (e.flags & GMAP_FLAG_GEOM_REVERSED) ? count - 1 - i : i;
    uint8_t b[8];
    if (n >= max) continue;
    rd(m, m->geom_off + e.geom + 4 + k * 8, b, 8);
    out[n].lat = (int32_t)le32(b);
    out[n].lon = (int32_t)le32(b + 4);
  }
  if (n < max) out[n] = gmap_node_point(m, e.target);
  return n + 1;
}

size_t gmap_name_at(gmap_t *m, uint32_t name_ref, char *out, size_t size) {
  size_t n = 0;
  if (size == 0) return 0;
  if (name_ref != GMAP_NONE) {
    /* Read in small blocks until the terminating NUL (names are short). */
    while (n + 1 < size) {
      uint8_t b[16];
      uint32_t i, len = (uint32_t)(size - 1 - n < sizeof(b) ? size - 1 - n : sizeof(b));
      if (m->names_off + name_ref + n + len > m->file_size) len = m->file_size - (m->names_off + name_ref + (uint32_t)n);
      if (len == 0 || !rd(m, m->names_off + name_ref + (uint32_t)n, b, len)) break;
      for (i = 0; i < len && b[i]; i++) out[n++] = (char)b[i];
      if (i < len) break;
    }
  }
  out[n] = '\0';
  return n;
}

size_t gmap_edge_name(gmap_t *m, uint32_t edge, char *out, size_t size) {
  return gmap_name_at(m, gmap_get_edge(m, edge).name, out, size);
}

void gmap_cell_of(const gmap_t *m, double lat, double lon, int32_t *row, int32_t *col) {
  const double cell = m->cell_size / GMAP_SCALE;
  *row = (int32_t)floor((lat - m->min_lat / GMAP_SCALE) / cell);
  *col = (int32_t)floor((lon - m->min_lon / GMAP_SCALE) / cell);
}

uint32_t gmap_edges_in_cell(gmap_t *m, int32_t row, int32_t col, uint32_t *out, uint32_t max) {
  const uint32_t ids = m->grid_off + 4;
  uint32_t id, lo = 0, hi = m->cell_count, starts, items, start, end, i;
  if (row < 0 || col < 0 || row >= m->grid_rows || col >= m->grid_cols || m->cell_count == 0) return 0;
  id = (uint32_t)row * m->grid_cols + (uint32_t)col;
  while (lo < hi) {  /* binary search among the sorted non-empty cells */
    const uint32_t mid = lo + (hi - lo) / 2;
    if (rd32(m, ids + mid * 4) < id) lo = mid + 1;
    else hi = mid;
  }
  if (lo == m->cell_count || rd32(m, ids + lo * 4) != id) return 0;
  starts = ids + m->cell_count * 4;
  items = starts + (m->cell_count + 1) * 4;
  start = rd32(m, starts + lo * 4);
  end = rd32(m, starts + (lo + 1) * 4);
  for (i = 0; i < end - start && i < max; i++) out[i] = rd32(m, items + (start + i) * 4);
  return end - start;
}

double gmap_distance_m(gmap_point a, gmap_point b) {
  /* The same flat approximation as roadpack.geo.local_xy and the navigator: meters per degree of latitude, and of
   * longitude scaled by the cosine of a's latitude. */
  const double k = cos(a.lat / GMAP_SCALE * 3.14159265358979323846 / 180.0);
  const double x = (b.lon - a.lon) / GMAP_SCALE * 111320.0 * k, y = (b.lat - a.lat) / GMAP_SCALE * 110540.0;
  return sqrt(x * x + y * y);
}

uint32_t gmap_nearest_node(gmap_t *m, double lat, double lon, int radius, uint32_t *scratch, uint32_t scratch_len) {
  const gmap_point p = {(int32_t)lround(lat * GMAP_SCALE), (int32_t)lround(lon * GMAP_SCALE)};
  int32_t row, col, r;
  gmap_cell_of(m, lat, lon, &row, &col);
  /* Grow the search ring by ring; stop at the first ring that has roads. */
  for (r = 1; r <= radius; r *= 2) {
    uint32_t best = GMAP_NONE;
    double best_d = 1e300;
    int32_t dr, dc;
    for (dr = -r; dr <= r; dr++) {
      for (dc = -r; dc <= r; dc++) {
        const uint32_t n = gmap_edges_in_cell(m, row + dr, col + dc, scratch, scratch_len);
        uint32_t i;
        for (i = 0; i < n && i < scratch_len; i++) {
          const gmap_edge e = gmap_get_edge(m, scratch[i]);
          const uint32_t ends[2] = {e.source, gmap_original(m, e.target)};
          int k;
          for (k = 0; k < 2; k++) {
            const double d = gmap_distance_m(p, gmap_node_point(m, ends[k]));
            if (d < best_d) best_d = d, best = ends[k];
          }
        }
      }
    }
    if (best != GMAP_NONE) return best;
  }
  return GMAP_NONE;
}

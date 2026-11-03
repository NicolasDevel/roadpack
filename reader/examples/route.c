/* Routes between two positions on a .gmap (a file or a directory of SD card chunks) through a page cache, the way a
 * device does, and prints the route.
 *
 *   route <map> <lat,lon> <lat,lon>
 *   route <map> --pairs     reads "start goal" node pairs from stdin, prints "cost edges e1 e2 ..." per pair
 *                           ("-" if there is no route)
 *   route <map> --multi     reads "start1 offset1 start2 offset2 goal" lines, prints "cost start_used" (or "-")
 *   route <map> --nearest   reads "lat lon" lines, prints the nearest junction (or "-")
 *   route <map> --edges     reads edge ids, prints "source target length weight speed flags original_target
 *                           lat,lon;lat,lon;... name"
 * The --modes are used by tests/check.py to compare everything with the Python reference.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gmap.h"
#include "gmap_cache.h"
#include "gmap_file.h"
#include "gmap_route.h"

#define PAGES 512 /* 2 MB of 4 KiB pages, like the ESP32-S3 navigator */
#define PAGE_SIZE 4096
#define TABLE_SIZE (1u << 14) /* ~770 KB for the router; 1 << 12 (~200 KB) was still enough across Colombia */
#define HEAP_CAP (1u << 14)
#define STACK_CAP 512
#define PATH_CAP 100000

static int parse_point(const char *s, double *lat, double *lon) { return sscanf(s, "%lf,%lf", lat, lon) == 2; }

static const char *status_text(gmap_route_status s) {
  switch (s) {
    case GMAP_ROUTE_OK: return "ok";
    case GMAP_ROUTE_NO_PATH: return "no route";
    case GMAP_ROUTE_FULL: return "search tables full";
    case GMAP_ROUTE_PATH_TOO_LONG: return "route longer than the path buffer";
    default: return "read error";
  }
}

int main(int argc, char **argv) {
  static gmap_file_t file;
  static gmap_cache_t cache;
  static gmap_t map;
  static gmap_router_t router;
  static uint32_t path[PATH_CAP], scratch[4096];
  void *cache_mem, *router_mem;
  uint32_t len, cost, i;
  gmap_route_status st;
  const char *mode = argc == 3 ? argv[2] : "";
  int pairs = strcmp(mode, "--pairs") == 0, nearest = strcmp(mode, "--nearest") == 0;
  int edges = strcmp(mode, "--edges") == 0, multi = strcmp(mode, "--multi") == 0;

  if (argc != 4 && !pairs && !nearest && !edges && !multi) {
    fprintf(stderr, "usage: %s <map> <lat,lon> <lat,lon>\n       %s <map> --pairs < pairs.txt\n", argv[0], argv[0]);
    return 2;
  }
  if (gmap_file_open(&file, argv[1]) != 0) {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  cache_mem = malloc(gmap_cache_memory(PAGES, PAGE_SIZE));
  router_mem = malloc(gmap_router_memory(TABLE_SIZE, HEAP_CAP, STACK_CAP));
  if (!cache_mem || !router_mem ||
      gmap_cache_init(&cache, gmap_file_read, &file, file.size, cache_mem, gmap_cache_memory(PAGES, PAGE_SIZE),
                      PAGE_SIZE) != 0 ||
      gmap_router_init(&router, router_mem, gmap_router_memory(TABLE_SIZE, HEAP_CAP, STACK_CAP), TABLE_SIZE, HEAP_CAP,
                       STACK_CAP) != 0)
    return 1;
  if (gmap_open(&map, gmap_cache_read, &cache, file.size) != GMAP_OK) {
    fprintf(stderr, "%s is not a valid .gmap (v2 or v3)\n", argv[1]);
    return 1;
  }

  if (multi) {
    unsigned long s1, o1, s2, o2, goal;
    while (scanf("%lu %lu %lu %lu %lu", &s1, &o1, &s2, &o2, &goal) == 5) {
      const uint32_t starts[2] = {(uint32_t)s1, (uint32_t)s2}, offsets[2] = {(uint32_t)o1, (uint32_t)o2};
      st = gmap_route_multi(&router, &map, starts, offsets, 2, (uint32_t)goal, path, PATH_CAP, &len, &cost);
      if (st == GMAP_ROUTE_OK) printf("%lu %lu\n", (unsigned long)cost, (unsigned long)router.start_used);
      else printf("-\n");
    }
    return 0;
  }
  if (nearest) {
    double lat, lon;
    while (scanf("%lf %lf", &lat, &lon) == 2) {
      const uint32_t n = gmap_nearest_node(&map, lat, lon, 8, scratch, 4096);
      if (n == GMAP_NONE) printf("-\n");
      else printf("%lu\n", (unsigned long)n);
    }
    return 0;
  }
  if (edges) {
    static gmap_point pts[4096];
    unsigned long id;
    char name[256];
    while (scanf("%lu", &id) == 1) {
      const gmap_edge e = gmap_get_edge(&map, (uint32_t)id);
      const uint32_t n = gmap_polyline(&map, (uint32_t)id, pts, 4096);
      printf("%lu %lu %lu %lu %u %u %lu ", (unsigned long)e.source, (unsigned long)e.target, (unsigned long)e.length_m,
             (unsigned long)e.weight_ds, (unsigned)e.speed_kmh, (unsigned)e.flags,
             (unsigned long)gmap_original(&map, e.target));
      for (i = 0; i < n && i < 4096; i++) printf("%s%ld,%ld", i ? ";" : "", (long)pts[i].lat, (long)pts[i].lon);
      gmap_edge_name(&map, (uint32_t)id, name, sizeof(name));
      printf(" %s\n", name);
    }
    return 0;
  }
  if (pairs) {
    unsigned long a, b;
    while (scanf("%lu %lu", &a, &b) == 2) {
      st = gmap_route(&router, &map, (uint32_t)a, (uint32_t)b, path, PATH_CAP, &len, &cost);
      if (st == GMAP_ROUTE_NO_PATH) {
        printf("-\n");
        continue;
      }
      if (st != GMAP_ROUTE_OK) {
        printf("error %s\n", status_text(st));
        continue;
      }
      printf("%lu %lu", (unsigned long)cost, (unsigned long)len);
      for (i = 0; i < len; i++) printf(" %lu", (unsigned long)path[i]);
      printf("\n");
    }
    return 0;
  }

  {
    double lat1, lon1, lat2, lon2, km = 0;
    uint32_t start, goal, misses;
    clock_t t0;
    char name[96], last[96] = "";
    if (!parse_point(argv[2], &lat1, &lon1) || !parse_point(argv[3], &lat2, &lon2)) {
      fprintf(stderr, "positions are lat,lon\n");
      return 2;
    }
    printf("%s: .gmap v%u, %lu nodes, %lu edges, %lu junction copies\n", argv[1], (unsigned)map.version,
           (unsigned long)map.node_count, (unsigned long)map.edge_count, (unsigned long)map.copy_count);
    start = gmap_nearest_node(&map, lat1, lon1, 4, scratch, 4096);
    goal = gmap_nearest_node(&map, lat2, lon2, 4, scratch, 4096);
    if (start == GMAP_NONE || goal == GMAP_NONE) {
      fprintf(stderr, "no roads near one of the positions\n");
      return 1;
    }
    gmap_cache_clear(&cache);  /* measure like a device that just started */
    misses = cache.misses;
    t0 = clock();
    st = gmap_route(&router, &map, start, goal, path, PATH_CAP, &len, &cost);
    if (st != GMAP_ROUTE_OK) {
      printf("%s\n", status_text(st));
      return 1;
    }
    printf("route: %lu edges, %.1f min, %lu nodes settled, %lu page reads (%.0f ms of CPU here)\n",
           (unsigned long)len, cost / 600.0, (unsigned long)router.settled, (unsigned long)(cache.misses - misses),
           (clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    for (i = 0; i < len; i++) {
      const gmap_edge e = gmap_get_edge(&map, path[i]);
      km += e.length_m / 1000.0;
      gmap_name_at(&map, e.name, name, sizeof(name));
      if (name[0] && strcmp(name, last) != 0) {
        printf("  %7.1f km  %s\n", km - e.length_m / 1000.0, name);
        strcpy(last, name);
      }
    }
    printf("  %7.1f km  arrive\n", km);
  }
  return 0;
}

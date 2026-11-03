# C reader and router

A portable C99 library to read `.gmap` files (versions 2 and 3, see [FORMAT.md](../FORMAT.md)) and route on them
with the contraction hierarchy, written for microcontrollers:

- **No allocation.** You give it the memory: a page cache (optional) and the router's tables.
- **Any storage.** Every access goes through a read function you provide: an SD card, a file, flash.
- **Portable.** Records are decoded byte by byte, so endianness and alignment do not matter.
- **Checked against the Python reference** (`roadpack.format`, `roadpack.routing`): same edges, geometry, names,
  nearest junctions and route costs, on Bogotá (v2 and v3) and all of Colombia.

| File | What it does |
|---|---|
| `gmap.h`, `gmap.c` | Header, nodes, edges, geometry, names, spatial grid, nearest junction, junction copies |
| `gmap_cache.h`, `gmap_cache.c` | Optional page cache in front of the read function (8-way set associative, LRU) |
| `gmap_route.h`, `gmap_route.c` | Contraction hierarchy query: one or several starts, the goal and its copies |
| `gmap_file.h`, `gmap_file.c` | Read function for stdio: a `.gmap` file or a directory of SD card chunks |
| `examples/route.c` | Command line example: route between two positions and print the streets |
| `tests/check.py` | Compares everything with the Python reference |

## Use

Copy the `.c` and `.h` files into your project (`gmap_file.c` only where there is stdio).

```c
#include "gmap.h"
#include "gmap_cache.h"
#include "gmap_route.h"

// Your storage: read `len` bytes at absolute offset `offset` of the map. Return 0 on success.
static int sd_read(void *ctx, uint32_t offset, void *dst, uint32_t len) { ... }

static uint8_t cache_mem[2 << 20], router_mem[800 << 10];  // in PSRAM on an ESP32-S3
static gmap_cache_t cache;
static gmap_router_t router;
static gmap_t map;

gmap_cache_init(&cache, sd_read, &my_sd, file_size, cache_mem, sizeof(cache_mem), 4096);
gmap_open(&map, gmap_cache_read, &cache, file_size);
gmap_router_init(&router, router_mem, sizeof(router_mem), 1 << 14, 1 << 14, 512);

uint32_t scratch[256], path[4096], len, cost;
uint32_t from = gmap_nearest_node(&map, 4.6097, -74.0817, 4, scratch, 256);
uint32_t to = gmap_nearest_node(&map, 10.3910, -75.4794, 4, scratch, 256);
if (gmap_route(&router, &map, from, to, path, 4096, &len, &cost) == GMAP_ROUTE_OK) {
  // path[0..len) are edge ids: gmap_get_edge(), gmap_polyline(), gmap_edge_name()
}
```

On the SD card the map is split into 4 MiB chunks (`/maps/<region>/0000.bin`, ...; see FORMAT.md): your read
function maps an offset to `offset / chunk_size` and `offset % chunk_size`. Opening a file is slow on FAT, so keep
the chunks you use open (on ESP-IDF, mind the `max_files` of the mount configuration).

**Turn restrictions** (`.gmap` v3) are junction copies: the router already starts the backward search at the goal
and its copies, so a route may end at a copy. When you compare junctions (to describe a maneuver, or to know whether
two edges meet), use `gmap_original()`; the exits of a junction are those of its original. See FORMAT.md, "Turn
restrictions".

**Read errors.** A failed read zero-fills what was asked and sets `map.io_error`; `gmap_route()` clears it when it
starts and returns `GMAP_ROUTE_IO` if any read of the query failed, so a bad SD read never becomes a wrong route.
Retry, or reopen the file.

## Memory

| What | Bytes | Notes |
|---|---:|---|
| `gmap_t` | ~100 | |
| Page cache | `pages × (page_size + 8)` | 512 pages of 4 KiB = 2 MB. Optional, but on an SD card it is what makes routing fast |
| Router | `2 × (16 × table_size + 8 × heap_cap) + 4 × stack_cap` | `1 << 14` tables: 770 KB; `1 << 12`: 200 KB |

`table_size` bounds the nodes a query reaches per side. Even across a country it is a few thousand: with
`1 << 12` all 300 random routes of the Colombia check were still identical. If a table, heap or the unpacking
stack is too small the query returns `GMAP_ROUTE_FULL`, never a wrong route.

## Build and check

```bash
make                                    # build/route
build/route ../out/colombia 4.5981,-74.0760 10.3910,-75.4794
python tests/check.py ../out/colombia   # with roadpack installed (the Python reference)
```

```
../out/colombia: .gmap v3, 1247302 nodes, 2997323 edges, 7314 junction copies
route: 2840 edges, 916.4 min, 515 nodes settled, 1124 page reads (4 ms of CPU here)
```

`check.py` compares 3,000 random edges (every field, the polyline and the name), the nearest junction of random
points, and routes between random nodes (copies included) and with two starts: same cost as the Python
contraction hierarchy query, and a valid path. It is part of the test suite (`tests/test_reader_c.py`), on
synthetic maps with turn restrictions.

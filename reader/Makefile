# The library is gmap.c, gmap_cache.c and gmap_route.c (plus gmap_file.c where there is stdio): copy them into a
# project, or build the example and the tests here.
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
LIB = gmap.c gmap_cache.c gmap_route.c gmap_file.c
HDR = gmap.h gmap_cache.h gmap_route.h gmap_file.h

build/route: examples/route.c $(LIB) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -I. -o $@ examples/route.c $(LIB) -lm

# Compares every route with the Python reference (roadpack.routing): MAP=<.gmap or chunk dir> make test
test: build/route
	python tests/check.py $(MAP)

clean:
	rm -rf build

.PHONY: test clean

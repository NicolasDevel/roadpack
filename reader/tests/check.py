#!/usr/bin/env python3
"""Checks the C reader and router against the Python reference (roadpack.format, roadpack.routing).

    python tests/check.py <.gmap file or chunk directory> [--routes N] [--edges N] [--points N] [--seed S]

Runs build/route (make builds it) in its --edges, --nearest and --pairs modes and compares:
- edges: every field, the polyline point by point, the name and the original junction of the target;
- nearest junction of random points (by distance: ties may pick either node);
- routes between random nodes, copies of junctions included: same cost as the Python contraction hierarchy query,
  and a valid path (consecutive edges, from the start to the goal or one of its copies, adding up to the cost);
- routes with two starts and a starting cost each: the cheaper of the two, and the start it left from.
Exits with status 1 on any mismatch.
"""
import argparse
import math
import random
import subprocess
import sys
from pathlib import Path

from roadpack.format import Gmap
from roadpack.format.spec import SCALE
from roadpack.geo import local_xy
from roadpack.routing import route_ch

BIN = Path(__file__).resolve().parent.parent / "build" / "route"


def run(gmap_path, mode, lines):
    out = subprocess.run([str(BIN), str(gmap_path), mode], input="\n".join(lines) + "\n", capture_output=True,
                         text=True, check=True).stdout
    return out.splitlines()


def check_edges(g, path, count, rng):
    ids = rng.sample(range(g.edge_count), min(count, g.edge_count))
    bad = 0
    for e, line in zip(ids, run(path, "--edges", map(str, ids))):
        fields = line.split(" ", 8)
        src, tgt, length, weight, speed, flags, orig = map(int, fields[:7])
        pts = [tuple(int(v) for v in p.split(",")) for p in fields[7].split(";")]
        name = fields[8] if len(fields) > 8 else ""
        expected_pts = [(round(lat * SCALE), round(lon * SCALE)) for lat, lon in g.polyline(e)]
        ok = ((src, tgt, length, weight, speed, flags, orig) ==
              (g.source[e], g.target[e], g.length[e], g.weight[e], g.speed[e], g.flags[e], g.original(g.target[e]))
              and pts == expected_pts and name == g.name(e))
        if not ok:
            bad += 1
            if bad <= 3:
                print(f"    edge {e}: C {line[:120]!r}")
    print(f"  edges: {len(ids)} compared, {bad} different")
    return bad


def check_nearest(g, path, count, rng):
    pts = [(rng.uniform(g.min_lat, g.max_lat), rng.uniform(g.min_lon, g.max_lon)) for _ in range(count)]
    bad = 0
    for (lat, lon), line in zip(pts, run(path, "--nearest", (f"{lat:.7f} {lon:.7f}" for lat, lon in pts))):
        try:
            expected = g.nearest_node(round(lat, 7), round(lon, 7))
        except ValueError:
            expected = None
        if line == "-" or expected is None:
            if (line == "-") != (expected is None):
                bad += 1
            continue
        dist = lambda n: math.hypot(*local_xy((lat, lon), g.point(n)))
        if g.is_copy(int(line)) or abs(dist(int(line)) - dist(expected)) > 0.01:
            bad += 1
    print(f"  nearest junction: {count} points, {bad} different")
    return bad


def check_routes(g, path, count, rng):
    pairs = [(rng.randrange(g.node_count), rng.randrange(g.node_count)) for _ in range(count)]
    bad = found = 0
    for (s, t), line in zip(pairs, run(path, "--pairs", (f"{s} {t}" for s, t in pairs))):
        ref, ref_cost = route_ch(g, s, t)
        if line == "-":
            if ref is not None:
                bad += 1
                print(f"    {s} -> {t}: no route in C, Python costs {ref_cost}")
            continue
        if line.startswith("error"):
            bad += 1
            print(f"    {s} -> {t}: {line}")
            continue
        cost, n, *edges = map(int, line.split())
        valid = (len(edges) == n and sum(g.weight[e] for e in edges) == cost
                 and all(g.target[a] == g.source[b] for a, b in zip(edges, edges[1:]))
                 and (not edges or (g.source[edges[0]] == s and g.original(g.target[edges[-1]]) == g.original(t)))
                 and (edges or s == t or g.original(s) == g.original(t)))
        if ref is None or cost != ref_cost or not valid:
            bad += 1
            if bad <= 5:
                print(f"    {s} -> {t}: C cost {cost} ({'valid' if valid else 'INVALID'} path), Python {ref_cost}")
        found += 1
    print(f"  routes: {count} pairs ({found} with a route), {bad} different")
    return bad


def check_multi(g, path, count, rng):
    cases = []
    for _ in range(count):
        s1, s2, t = (rng.randrange(g.node_count) for _ in range(3))
        cases.append((s1, rng.randrange(0, 3000), s2, rng.randrange(0, 3000), t))
    bad = 0
    for (s1, o1, s2, o2, t), line in zip(cases, run(path, "--multi", (" ".join(map(str, c)) for c in cases))):
        c1, c2 = route_ch(g, s1, t)[1], route_ch(g, s2, t)[1]
        best = min(o1 + c1, o2 + c2)
        if line == "-":
            bad += best != math.inf
            continue
        cost, used = map(int, line.split())
        if cost != best or (o1 + c1, o2 + c2)[used] != best:
            bad += 1
    print(f"  routes with two starts: {count} cases, {bad} different")
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gmap", type=Path)
    parser.add_argument("--routes", type=int, default=300)
    parser.add_argument("--edges", type=int, default=3000)
    parser.add_argument("--points", type=int, default=300)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if not BIN.exists():
        sys.exit(f"{BIN} is missing: run make")
    g = Gmap(args.gmap)
    rng = random.Random(args.seed)
    print(f"{args.gmap}: .gmap v{g.version}, {g.node_count:,} nodes, {g.copy_count:,} junction copies")
    bad = check_edges(g, args.gmap, args.edges, rng)
    bad += check_nearest(g, args.gmap, args.points, rng)
    bad += check_routes(g, args.gmap, args.routes, rng)
    bad += check_multi(g, args.gmap, args.routes // 3, rng)
    print("all equal" if not bad else f"{bad} MISMATCHES")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

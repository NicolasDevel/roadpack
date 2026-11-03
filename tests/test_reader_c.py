"""The C reader and router (reader/) against the Python reference, on synthetic maps: reader/tests/check.py compares
edges, geometry, names, nearest junctions and routes (one start and two)."""
import random
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from roadpack.osm import PROFILES
from roadpack.pipeline import build

from osm_builder import LAT0, LON0, grid_network

READER = Path(__file__).resolve().parent.parent / "reader"

pytestmark = pytest.mark.skipif(not (shutil.which("make") and (shutil.which("cc") or shutil.which("gcc"))),
                                reason="needs make and a C compiler")


@pytest.fixture(scope="module")
def route_binary():
    subprocess.run(["make", "-s", "-C", str(READER)], check=True)
    return READER / "build" / "route"


def check(gmap_path, routes=300):
    result = subprocess.run([sys.executable, str(READER / "tests" / "check.py"), str(gmap_path), "--routes", str(routes),
                             "--edges", "2000", "--points", "300"], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all equal" in result.stdout


def test_town(route_binary, town_map):
    check(town_map[2])


@pytest.mark.parametrize("seed", [1, 2])
def test_grid_with_turn_restrictions(route_binary, tmp_path, seed):
    """A street grid with random turn restrictions: routes go through junction copies and may end at one."""
    osm, ids = grid_network(10, seed=seed)
    way_of = {frozenset(refs): wid for wid, refs, _ in osm.ways}
    rng = random.Random(seed)
    for _ in range(60):
        r, c = rng.randrange(1, 9), rng.randrange(1, 9)
        v = ids[r][c]
        arms = [ids[r - 1][c], ids[r + 1][c], ids[r][c - 1], ids[r][c + 1]]
        u, w = rng.sample(arms, 2)
        kind = rng.choice(["no_left_turn", "no_right_turn", "only_straight_on"])
        osm.restriction(kind, way_of[frozenset((u, v))], v, way_of[frozenset((v, w))])
    out = tmp_path / "grid.gmap"
    stats = build(osm.write(tmp_path / "grid.osm"), (LAT0 - 0.01, LON0 - 0.01, LAT0 + 0.02, LON0 + 0.02),
                  PROFILES["motorcycle"], out, chunk_dir=tmp_path / "chunks", log=lambda _: None)
    assert stats["node_copies"] > 0
    check(out, routes=600)
    check(tmp_path / "chunks", routes=100)  # the same map read from its SD card chunks

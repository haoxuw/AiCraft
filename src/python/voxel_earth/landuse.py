"""landuse.py — fetch OSM features for a region, classify each chunk.

Output is a packed byte array, one byte per chunk, mapping
(chunk_x, chunk_z) → category enum. Saved next to the VEAR region as
`landuse.json` so the C++ world template can mmap-read it at boot.

Categories (must stay in sync with C++ palette code):

  0  unknown      — no OSM polygon overlapped this chunk
  1  city_center  — landuse=commercial / dense buildings
  2  residential  — landuse=residential
  3  industrial   — landuse=industrial
  4  forest       — landuse=forest, natural=wood
  5  park         — leisure=park, landuse=recreation_ground
  6  farmland     — landuse=farmland / orchard / vineyard
  7  water        — natural=water
  8  beach        — natural=beach

Priority (highest wins when multiple overlap a chunk):
  water > beach > city_center > industrial > residential > forest > park > farmland > unknown
"""
from __future__ import annotations

import base64
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

# osmnx + geopandas are imported lazily so `python -m voxel_earth` keeps
# starting fast for non-landuse subcommands.

CATEGORIES = [
    "unknown",       # 0
    "city_center",   # 1
    "residential",   # 2
    "industrial",    # 3
    "forest",        # 4
    "park",          # 5
    "farmland",      # 6
    "water",         # 7
    "beach",         # 8
]
CATEGORY_INDEX = {c: i for i, c in enumerate(CATEGORIES)}

# Higher number wins when polygons overlap.
PRIORITY = {
    "unknown":     0,
    "farmland":    1,
    "park":        2,
    "forest":      3,
    "residential": 4,
    "industrial":  5,
    "city_center": 6,
    "beach":       7,
    "water":       8,
}

# OSM tag → category. The dict-of-list-of-values form is what osmnx wants.
# Order doesn't matter; resolution is by priority above.
OSM_TAG_QUERY = {
    "landuse":  ["commercial", "retail", "industrial", "residential",
                 "forest",     "recreation_ground", "farmland",
                 "orchard",    "vineyard"],
    "natural":  ["water", "wood", "beach", "wetland"],
    "leisure":  ["park"],
    "waterway": ["river", "stream", "canal"],
    "place":    ["city", "town", "suburb"],
    "building": True,                              # density signal
}


def _tag(tags: dict, key: str):
    """tags.get() that treats pandas NaN / empty string as missing.

    osmnx returns one column per queried tag for every row, filling NaN where
    the feature doesn't carry that tag — so a naive `key in tags` reports
    True for *every* feature. Need to check the value too.
    """
    v = tags.get(key)
    if v is None:
        return None
    # pandas NaN: only NaN != itself.
    try:
        if v != v:    # NaN
            return None
    except Exception:
        pass
    if isinstance(v, str) and not v:
        return None
    return v


def _classify_row(tags: dict) -> Optional[str]:
    """Map a single OSM feature's tags to one of our categories."""
    natural  = _tag(tags, "natural")
    waterway = _tag(tags, "waterway")
    leisure  = _tag(tags, "leisure")
    landuse  = _tag(tags, "landuse")
    if natural == "water" or waterway is not None:
        return "water"
    if natural == "beach":
        return "beach"
    if landuse in ("commercial", "retail"):
        return "city_center"
    if landuse == "industrial":
        return "industrial"
    if landuse == "residential":
        return "residential"
    if landuse == "forest" or natural == "wood":
        return "forest"
    if landuse == "recreation_ground" or leisure == "park":
        return "park"
    if landuse in ("farmland", "orchard", "vineyard", "meadow"):
        return "farmland"
    return None


@dataclass
class LanduseGrid:
    chunk_size_blocks: int   # voxels per chunk (16)
    voxel_size_m:      float # metres per voxel (1.0)
    origin_x_blocks:   int   # region bbox_min[0]
    origin_z_blocks:   int   # region bbox_min[2]
    nx_chunks:         int
    nz_chunks:         int
    data:              bytearray   # length = nx_chunks * nz_chunks

    def set(self, ix: int, iz: int, cat: int) -> None:
        if not (0 <= ix < self.nx_chunks and 0 <= iz < self.nz_chunks):
            return
        self.data[iz * self.nx_chunks + ix] = cat

    def get(self, ix: int, iz: int) -> int:
        if not (0 <= ix < self.nx_chunks and 0 <= iz < self.nz_chunks):
            return 0
        return self.data[iz * self.nx_chunks + ix]

    def to_json(self, lat: float, lng: float, radius_m: float) -> dict:
        return {
            "version": 1,
            "lat": lat, "lng": lng, "radius_m": radius_m,
            "categories": CATEGORIES,
            "voxel_size_m": self.voxel_size_m,
            "chunk_size_blocks": self.chunk_size_blocks,
            "origin_x_blocks": self.origin_x_blocks,
            "origin_z_blocks": self.origin_z_blocks,
            "nx_chunks": self.nx_chunks,
            "nz_chunks": self.nz_chunks,
            "data_b64": base64.b64encode(bytes(self.data)).decode("ascii"),
        }


def _enu_xz_per_metre(lat_deg: float, lng_deg: float) -> tuple[float, float]:
    """Return (metres-per-degree-lng, metres-per-degree-lat) at this latitude."""
    a = 6378137.0
    f = 1.0 / 298.257223563
    e2 = f * (2.0 - f)
    rlat = math.radians(lat_deg)
    sin_lat = math.sin(rlat)
    n = a / math.sqrt(1.0 - e2 * sin_lat * sin_lat)
    m = a * (1.0 - e2) / (1.0 - e2 * sin_lat * sin_lat) ** 1.5
    return (n * math.cos(rlat) * math.pi / 180.0,   # m per deg lng
            m * math.pi / 180.0)                    # m per deg lat


def build_landuse_grid(lat: float, lng: float, radius_m: float,
                       origin_x_blocks: int, origin_z_blocks: int,
                       nx_chunks: int, nz_chunks: int,
                       chunk_size_blocks: int = 16,
                       voxel_size_m: float = 1.0,
                       progress_cb=None) -> LanduseGrid:
    """Fetch OSM polygons in a square around (lat, lng) of side 2*radius_m,
    then classify every chunk in the grid by the highest-priority category
    whose polygon contains the chunk centre."""
    import osmnx as ox            # noqa
    import geopandas as gpd       # noqa
    from shapely.geometry import Point  # noqa

    # 1. Pull features. osmnx returns a GeoDataFrame in EPSG:4326 (lat/lng).
    print(f"[landuse] osmnx features_from_point ({lat:.5f}, {lng:.5f}, dist={radius_m:.0f}m)...")
    gdf = ox.features.features_from_point((lat, lng), tags=OSM_TAG_QUERY,
                                          dist=int(radius_m))
    print(f"[landuse] got {len(gdf)} features")

    # 2. Project lat/lng → metres in our local ENU frame (centred on lat/lng).
    m_per_dlng, m_per_dlat = _enu_xz_per_metre(lat, lng)

    # 3. Build per-category MultiPolygons.
    cat_polys: dict[str, list] = {c: [] for c in CATEGORIES if c != "unknown"}
    for _, row in gdf.iterrows():
        cat = _classify_row(row.to_dict())
        if cat is None:
            continue
        geom = row.geometry
        if geom is None:
            continue
        cat_polys[cat].append(geom)

    # Normalise to shapely union per category.
    cat_union: dict[str, object] = {}
    from shapely.ops import unary_union
    for cat, polys in cat_polys.items():
        if not polys:
            continue
        try:
            cat_union[cat] = unary_union(polys)
        except Exception as e:
            print(f"[landuse] union({cat}) skipped: {e}")

    # 4. Iterate chunks, point-in-polygon for chunk centre.
    grid = LanduseGrid(
        chunk_size_blocks=chunk_size_blocks,
        voxel_size_m=voxel_size_m,
        origin_x_blocks=origin_x_blocks,
        origin_z_blocks=origin_z_blocks,
        nx_chunks=nx_chunks, nz_chunks=nz_chunks,
        data=bytearray(nx_chunks * nz_chunks),
    )
    chunk_m = chunk_size_blocks * voxel_size_m
    total = nx_chunks * nz_chunks
    done = 0
    for iz in range(nz_chunks):
        for ix in range(nx_chunks):
            # Chunk centre in metres relative to (lat, lng).
            cx_m = (origin_x_blocks + (ix + 0.5) * chunk_size_blocks) * voxel_size_m
            cz_m = (origin_z_blocks + (iz + 0.5) * chunk_size_blocks) * voxel_size_m
            # Convert to lat/lng.
            dlng = cx_m / m_per_dlng
            dlat = -cz_m / m_per_dlat   # +z in ENU is south? sign per pipeline
            pt = Point(lng + dlng, lat + dlat)

            best_pri = -1
            best_cat = 0
            for cat, geom in cat_union.items():
                if PRIORITY[cat] <= best_pri:
                    continue
                if geom.contains(pt):
                    best_pri = PRIORITY[cat]
                    best_cat = CATEGORY_INDEX[cat]
            grid.set(ix, iz, best_cat)
            done += 1
            if progress_cb and (done % 1024 == 0 or done == total):
                progress_cb(done, total)

    return grid


def write_landuse_json(path: str | Path, grid: LanduseGrid,
                       lat: float, lng: float, radius_m: float) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open("w") as f:
        json.dump(grid.to_json(lat, lng, radius_m), f, indent=2)


def category_histogram(grid: LanduseGrid) -> dict[str, int]:
    counts = [0] * len(CATEGORIES)
    for b in grid.data:
        counts[b] += 1
    return {CATEGORIES[i]: counts[i] for i in range(len(CATEGORIES))}

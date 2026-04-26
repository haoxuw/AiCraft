"""download.py — BFS the Google Photorealistic 3D Tiles tree, fetch leaf GLBs.

Ports TilesDownloader.java + tile_downloader.js (the BFS + sphere-cull half).
GLB rotate / Draco decode is the next stage — handled in C++.

Quotas (free tier)
──────────────────
  Billing is per *root tileset request*; child tile fetches are unmetered.
  We hold one 3-hour session token in ~/.voxel/google/session.json so a
  whole afternoon of /visit calls is still one billable event.
"""
from __future__ import annotations

import json
import math
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Optional

from .cache import VoxelCache, obb_sha1


@dataclass(frozen=True)
class Tile:
    url: str
    obb: tuple[float, ...]   # 12 floats: cx,cy,cz, h1xyz, h2xyz, h3xyz

    @property
    def stable_id(self) -> str:
        return obb_sha1(self.obb)

ROOT_URL = "https://tile.googleapis.com/v1/3dtiles/root.json"
WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)


def cartesian_from_degrees(lon_deg: float, lat_deg: float, h: float = 0.0) -> tuple[float, float, float]:
    rad_lat = math.radians(lat_deg)
    rad_lon = math.radians(lon_deg)
    sin_lat = math.sin(rad_lat)
    n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    x = (n + h) * math.cos(rad_lat) * math.cos(rad_lon)
    y = (n + h) * math.cos(rad_lat) * math.sin(rad_lon)
    z = (n * (1.0 - WGS84_E2) + h) * sin_lat
    return (x, y, z)


@dataclass(frozen=True)
class Sphere:
    cx: float
    cy: float
    cz: float
    r: float

    def intersects(self, other: "Sphere") -> bool:
        dx, dy, dz = other.cx - self.cx, other.cy - self.cy, other.cz - self.cz
        return math.sqrt(dx * dx + dy * dy + dz * dz) < self.r + other.r


def obb_to_sphere(box: list[float]) -> Sphere:
    """3D Tiles boundingVolume.box → enclosing sphere (no transform; coarse cull)."""
    cx, cy, cz = box[0], box[1], box[2]
    h1 = (box[3], box[4], box[5])
    h2 = (box[6], box[7], box[8])
    h3 = (box[9], box[10], box[11])
    minv = [float("inf")] * 3
    maxv = [float("-inf")] * 3
    for i in range(8):
        s1 = 1 if (i & 1) else -1
        s2 = 1 if (i & 2) else -1
        s3 = 1 if (i & 4) else -1
        for k in range(3):
            v = (cx, cy, cz)[k] + s1 * h1[k] + s2 * h2[k] + s3 * h3[k]
            if v < minv[k]:
                minv[k] = v
            if v > maxv[k]:
                maxv[k] = v
    mid = [(minv[k] + maxv[k]) * 0.5 for k in range(3)]
    diag = math.sqrt(sum((maxv[k] - minv[k]) ** 2 for k in range(3)))
    return Sphere(mid[0], mid[1], mid[2], 0.5 * diag)


class _SessionRef:
    __slots__ = ("value",)

    def __init__(self, value: Optional[str] = None) -> None:
        self.value = value


def _annotate_url(url: str, key: str, session: _SessionRef) -> str:
    parsed = urllib.parse.urlparse(url)
    qs = dict(urllib.parse.parse_qsl(parsed.query, keep_blank_values=True))
    captured = qs.get("session")
    if captured:
        session.value = captured
    if "key" not in qs:
        qs["key"] = key
    if session.value and "session" not in qs:
        qs["session"] = session.value
    return urllib.parse.urlunparse(parsed._replace(query=urllib.parse.urlencode(qs)))


def _fetch_json(url: str, timeout: float) -> dict:
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def _resolve(child_uri: str, base_url: str) -> str:
    return urllib.parse.urljoin(base_url, child_uri)


def discover(api_key: str, lat: float, lng: float, radius: float,
             *, elevation: float = 0.0, timeout: float = 30.0,
             cache: Optional[VoxelCache] = None,
             use_cache: bool = True) -> list[Tile]:
    """BFS the tileset, return Tile(url, obb) for each leaf intersecting the
    search sphere. The OBB gives a session-stable identity for caching;
    Google's URL paths embed the session token and rotate on each visit.

    If a discover cache exists for (lat, lng, radius) AND every cached OBB
    already has its GLB on disk, this returns immediately with URL="" tiles
    — caller's downloader skips them as cache hits, so zero Google calls fire.
    """
    cache = cache or VoxelCache()

    # Fast path: replay a previous discover. Tile.url stays empty; download_all
    # only follows URLs for misses, so a fully cached region costs zero calls.
    dpath = cache.discover_path(lat, lng, radius)
    if use_cache and dpath.is_file():
        try:
            entries = json.loads(dpath.read_text(encoding="utf-8"))
            cached_tiles: list[Tile] = []
            all_present = True
            for e in entries:
                obb = tuple(e["obb"])
                cached_tiles.append(Tile(url="", obb=obb))
                if not cache.glb_path(list(obb)).is_file():
                    all_present = False
            if all_present and cached_tiles:
                print(f"[discover] cache hit ({len(cached_tiles)} tiles, all GLBs on disk) "
                      f"— skipping Google BFS")
                return cached_tiles
        except (OSError, json.JSONDecodeError, KeyError):
            pass  # fall through to live BFS

    cx, cy, cz = cartesian_from_degrees(lng, lat, elevation)
    region = Sphere(cx, cy, cz, radius)
    session = _SessionRef(None)  # root.json rejects session=; capture it from child URLs

    tiles: list[Tile] = []

    def walk_node(node: dict, base_url: str) -> None:
        bv = node.get("boundingVolume") or {}
        box = bv.get("box")
        if box is not None:
            if not region.intersects(obb_to_sphere(box)):
                return
        children = node.get("children") or []
        if children:
            for c in children:
                walk_node(c, base_url)
            return
        contents = []
        if (c := node.get("content")) and c.get("uri"):
            contents.append(c)
        contents.extend(node.get("contents") or [])
        for c in contents:
            uri = c.get("uri")
            if not uri:
                continue
            full = _annotate_url(_resolve(uri, base_url), api_key, session)
            if full.split("?", 1)[0].endswith(".glb"):
                if box is None:
                    continue  # no OBB → no stable cache key; skip
                tiles.append(Tile(url=full, obb=tuple(box)))
            else:
                fetch_tileset(full)

    def fetch_tileset(url: str) -> None:
        try:
            data = _fetch_json(url, timeout=timeout)
        except Exception as e:  # noqa: BLE001 — surface URL with the error
            print(f"[warn] sub-tileset fetch failed: {url} ({e})")
            return
        root = data.get("root")
        if not root:
            return
        walk_node(root, url)

    root_url = (ROOT_URL + "?" + urllib.parse.urlencode({"key": api_key}))
    fetch_tileset(root_url)
    if session.value:
        cache.save_session(session.value)

    # Persist the OBB list so a follow-up discover for the same region skips
    # the BFS entirely.
    try:
        dpath.parent.mkdir(parents=True, exist_ok=True)
        dpath.write_text(json.dumps([{"obb": list(t.obb)} for t in tiles], indent=2),
                         encoding="utf-8")
    except OSError as e:
        print(f"[warn] could not write discover cache: {e}")
    return tiles


def download_all(tiles: list[Tile], cache: VoxelCache, *,
                 parallel: int = 10, timeout: float = 60.0) -> tuple[int, int]:
    """Fetch each tile's URL into cache.glb_path(obb). Returns (downloaded, skipped)."""
    cache.init()
    todo = [(t, cache.glb_path(list(t.obb))) for t in tiles]
    skipped = sum(1 for _, p in todo if p.is_file())
    pending = [(t, p) for t, p in todo if not p.is_file()]

    def fetch_one(tile: Tile, dst) -> bool:
        if not tile.url:
            # Discover cache hit but GLB also missing — orphan; skip silently.
            return False
        try:
            with urllib.request.urlopen(tile.url, timeout=timeout) as resp:
                data = resp.read()
            tmp = dst.with_suffix(dst.suffix + ".part")
            tmp.write_bytes(data)
            tmp.rename(dst)
            return True
        except Exception as e:  # noqa: BLE001
            print(f"[warn] download failed: {tile.url} ({e})")
            return False

    downloaded = 0
    with ThreadPoolExecutor(max_workers=parallel) as ex:
        for ok in as_completed(ex.submit(fetch_one, t, p) for t, p in pending):
            if ok.result():
                downloaded += 1
    return downloaded, skipped

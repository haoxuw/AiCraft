"""CLI entry: python -m voxel_earth <command> ..."""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from .api import ApiKeyMissing, GoogleApi, GoogleApiError
from .cache import VoxelCache
from . import download as _dl


def cmd_init(args: argparse.Namespace) -> int:
    cache = VoxelCache()
    cache.init()
    print(f"cache root: {cache.root}")
    print(f"api key:    {'set' if cache.read_api_key() else 'NOT SET (use `python -m voxel_earth set-key <KEY>`)'}")
    return 0


def cmd_set_key(args: argparse.Namespace) -> int:
    cache = VoxelCache()
    cache.write_api_key(args.key)
    print(f"wrote {cache.api_key_path}")
    return 0


def cmd_geocode(args: argparse.Namespace) -> int:
    api = GoogleApi(VoxelCache())
    try:
        lat, lng = api.geocode(args.query, use_cache=not args.no_cache)
    except ApiKeyMissing as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except GoogleApiError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"{lat:.6f},{lng:.6f}")
    return 0


def cmd_download(args: argparse.Namespace) -> int:
    cache = VoxelCache()
    api = GoogleApi(cache)
    try:
        if args.location:
            lat, lng = api.geocode(args.location)
            print(f"resolved {args.location!r} → {lat:.6f},{lng:.6f}")
        else:
            lat, lng = args.lat, args.lng
        elev = api.elevation(lat, lng) if args.elevation else 0.0
        height = args.height if args.height else 2.0 * args.radius
        tiles = _dl.discover(cache.read_api_key(), lat, lng, args.radius,
                             height=height, elevation=elev, cache=cache)
    except ApiKeyMissing as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except GoogleApiError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"discovered {len(tiles)} tiles in cylinder "
          f"radius_xz={args.radius:g}m height={height:g}m")
    if args.dry_run or not tiles:
        return 0
    dl, skip = _dl.download_all(tiles, cache, parallel=args.parallel)
    print(f"downloaded {dl}, cache hits {skip}, total cached: "
          f"{len(list((cache.root / 'google' / 'glb').glob('*.glb')))}")
    return 0


def _read_vear_header(path: Path) -> dict:
    """Read just the VEAR header — 68 bytes, fixed layout — for bbox info.

    See src/platform/server/voxel_earth/region.cpp::OnDiskHeader.
    """
    with path.open("rb") as f:
        magic   = f.read(4)
        if magic != b"VEAR":
            raise ValueError(f"{path}: not a VEAR file")
        version = struct.unpack("<I", f.read(4))[0]
        origin  = struct.unpack("<3d", f.read(24))
        vsize_mm = struct.unpack("<I", f.read(4))[0]
        bmin    = struct.unpack("<3i", f.read(12))
        bmax    = struct.unpack("<3i", f.read(12))
        count   = struct.unpack("<I", f.read(4))[0]
        return dict(version=version, origin_ecef=origin,
                    voxel_size_mm=vsize_mm,
                    bbox_min=bmin, bbox_max=bmax, count=count)


def cmd_landuse(args: argparse.Namespace) -> int:
    from . import landuse as _lu

    region_path = Path(args.region).expanduser()
    if not region_path.exists():
        print(f"error: region file not found: {region_path}", file=sys.stderr)
        return 1
    h = _read_vear_header(region_path)
    bmin, bmax = h["bbox_min"], h["bbox_max"]

    cache = VoxelCache()
    api = GoogleApi(cache)
    try:
        if args.location:
            lat, lng = api.geocode(args.location)
        else:
            lat, lng = args.lat, args.lng
    except (ApiKeyMissing, GoogleApiError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    voxel_m = h["voxel_size_mm"] / 1000.0
    chunk_blocks = args.chunk_size
    nx_blocks = bmax[0] - bmin[0] + 1
    nz_blocks = bmax[2] - bmin[2] + 1
    nx_chunks = (nx_blocks + chunk_blocks - 1) // chunk_blocks
    nz_chunks = (nz_blocks + chunk_blocks - 1) // chunk_blocks
    print(f"[landuse] region {region_path.name}: bbox xz {nx_blocks}x{nz_blocks} "
          f"-> {nx_chunks}x{nz_chunks} chunks")

    radius_m = max(nx_blocks, nz_blocks) * voxel_m * 0.6  # tight square + slack
    grid = _lu.build_landuse_grid(
        lat=lat, lng=lng, radius_m=radius_m,
        origin_x_blocks=bmin[0], origin_z_blocks=bmin[2],
        nx_chunks=nx_chunks, nz_chunks=nz_chunks,
        chunk_size_blocks=chunk_blocks, voxel_size_m=voxel_m,
        progress_cb=lambda d, t: print(f"  classified {d}/{t} chunks") if d % (1024*8) == 0 else None,
    )
    out_path = region_path.parent / "landuse.json"
    _lu.write_landuse_json(out_path, grid, lat, lng, radius_m)
    hist = _lu.category_histogram(grid)
    print(f"[landuse] wrote {out_path}")
    for name, count in sorted(hist.items(), key=lambda kv: -kv[1]):
        if count == 0:
            continue
        pct = 100.0 * count / len(grid.data)
        print(f"  {name:14s} {count:6d}  ({pct:5.1f}%)")
    return 0


def _slug_lat_lng(lat: float, lng: float, radius_m: float) -> str:
    """Stable cache key from coords. Pure-coords (no place name) so the
    same lat/lng always hits the same shard regardless of how the user
    specified it; rounded to 4 decimals (~11 m) so trivial geocode jitter
    doesn't fragment the cache."""
    return f"lat{lat:.4f}_lng{lng:.4f}_r{int(round(radius_m))}"


def cmd_world(args: argparse.Namespace) -> int:
    """End-to-end: geocode → cache key → bake-if-missing → exec the engine.

    `make world LAT=… LNG=… RADIUS=…` calls this. Toronto / Wonderland
    Makefile aliases just pre-fill --location.
    """
    cache = VoxelCache()
    api = GoogleApi(cache)
    try:
        if args.location:
            lat, lng = api.geocode(args.location)
            print(f"[world] resolved {args.location!r} → {lat:.6f},{lng:.6f}")
        else:
            lat, lng = args.lat, args.lng
    except (ApiKeyMissing, GoogleApiError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    slug = _slug_lat_lng(lat, lng, args.radius)
    region_dir = cache.root / "regions" / slug
    region_path = region_dir / "blocks.bin"
    landuse_path = region_dir / "landuse.json"

    bake_bin = Path(args.build_dir) / "solarium-voxel-bake"
    game_bin = Path(args.build_dir) / "solarium-ui-vk"
    if not game_bin.exists():
        print(f"error: {game_bin} not found — run `make build` first",
              file=sys.stderr)
        return 1

    # 1. Download GLBs (skipped silently if cache covers).
    if not region_path.exists() or args.force:
        height = args.height if args.height else 2.0 * args.radius
        try:
            elev = api.elevation(lat, lng) if args.elevation else 0.0
            tiles = _dl.discover(cache.read_api_key(), lat, lng, args.radius,
                                 height=height, elevation=elev, cache=cache)
        except (ApiKeyMissing, GoogleApiError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        if not tiles:
            print(f"error: discover returned 0 tiles for ({lat},{lng}) "
                  f"r={args.radius}", file=sys.stderr)
            return 1
        print(f"[world] downloading {len(tiles)} tiles "
              f"(radius={args.radius:g}m height={height:g}m)…")
        _dl.download_all(tiles, cache, parallel=args.parallel)

        # 2. Bake — pass an explicit GLB list so the bake processes ONLY
        # this location's tiles. Without this the bake scans the entire
        # ~/.voxel/google/glb/ directory and the bbox spans every location
        # ever downloaded (Toronto + Wonderland → 17 km × 20 km, 27 GB
        # interior-fill bitmaps, OOM).
        if not bake_bin.exists():
            print(f"error: {bake_bin} not found — run `make build` first",
                  file=sys.stderr)
            return 1
        region_dir.mkdir(parents=True, exist_ok=True)
        glb_list_path = region_dir / "glb_list.txt"
        with glb_list_path.open("w") as f:
            f.write(f"# {len(tiles)} GLB tiles for ({lat},{lng}) r={args.radius}\n")
            for t in tiles:
                f.write(str(cache.glb_path(list(t.obb))) + "\n")
        # Tile shards land in the SHARED ~/.voxel/tiles/ tree, keyed by
        # regional anchor (floor(lat), floor(lng)) so two bakes whose
        # centres fall in the same 1° square write to the same files.
        # Independent shards: any subset can be copied/shared. Region
        # anchor in v1 is informational; voxel coords stay in this bake's
        # own ECEF frame until the cross-bake-merge change lands (commit
        # 4 of the tile-shard plan).
        tile_out = cache.root / "tiles"
        rlat = int(__import__("math").floor(lat))
        rlng = int(__import__("math").floor(lng))
        bake_cmd = [str(bake_bin),
                    "--glb-list", str(glb_list_path),
                    "--out", str(region_path),
                    "--voxel-size", str(args.voxel_size),
                    "--tile-out", str(tile_out),
                    "--region-lat", str(rlat),
                    "--region-lng", str(rlng)]
        print(f"[world] {' '.join(bake_cmd)}")
        rc = subprocess.call(bake_cmd)
        if rc != 0:
            print(f"error: bake failed with exit {rc}", file=sys.stderr)
            return rc

    # 3. Landuse (skipped if file already there).
    if not landuse_path.exists() or args.force:
        rc = subprocess.call([sys.executable, "-m", "voxel_earth", "landuse",
                              "--region", str(region_path),
                              "--lat", str(lat), "--lng", str(lng)])
        if rc != 0:
            print(f"warning: landuse step failed (exit {rc}); "
                  "continuing without zone map", file=sys.stderr)

    # 4. Auto-offset Y so the bake floor sits just above world-y 0. Reading
    # the VEAR header is the cheapest way to know the bake's vertical extent
    # without parsing the whole file.
    h = _read_vear_header(region_path)
    bake_floor = h["bbox_min"][1]
    offset_y = -bake_floor + 10  # 10-block headroom under the bake floor

    # 5. Exec the engine. Env vars drive voxel_earth_dynamic.py.
    template_index = args.template_index
    import math
    rlat = int(math.floor(lat))
    rlng = int(math.floor(lng))
    env = os.environ.copy()
    env["SOLARIUM_VOXEL_REGION"]     = str(region_path)
    env["SOLARIUM_VOXEL_TILE_DIR"]   = str(cache.root / "tiles")
    env["SOLARIUM_VOXEL_REGION_LAT"] = str(rlat)
    env["SOLARIUM_VOXEL_REGION_LNG"] = str(rlng)
    env["SOLARIUM_VOXEL_OFFSET_Y"]   = str(offset_y)
    env["SOLARIUM_VOXEL_NAME"]       = args.name or args.location or slug

    cmd = [str(game_bin.resolve()), "--skip-menu",
           "--cef-menu", "--template", str(template_index)]
    print(f"[world] launching: {' '.join(cmd)}")
    print(f"[world]   region={region_path}")
    print(f"[world]   offset_y={offset_y}")
    # cwd matters: the engine resolves artifacts/ relative to cwd; spawn
    # from the build dir so artifacts are next to the binary.
    return subprocess.call(cmd, cwd=str(game_bin.parent), env=env)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="voxel_earth")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init", help="create ~/.voxel/ layout")

    p_set = sub.add_parser("set-key", help="store Google API key in ~/.voxel/api_key")
    p_set.add_argument("key")

    p_geo = sub.add_parser("geocode", help="text → lat,lng")
    p_geo.add_argument("query", help='e.g. "Toronto" or "Eiffel Tower, Paris"')
    p_geo.add_argument("--no-cache", action="store_true")

    p_dl = sub.add_parser("download", help="BFS 3D Tiles + fetch GLBs into ~/.voxel/google/glb/")
    p_dl.add_argument("--location", help='geocoded place name; alternative to --lat/--lng')
    p_dl.add_argument("--lat", type=float)
    p_dl.add_argument("--lng", type=float)
    p_dl.add_argument("--radius", type=float, default=200.0,
                      help="horizontal radius in meters (default 200)")
    p_dl.add_argument("--height", type=float, default=0.0,
                      help="vertical extent in meters (default = 2·radius — sphere-like). "
                           "Set higher to capture tall structures, e.g. 1200 for CN Tower.")
    p_dl.add_argument("--elevation", action="store_true",
                      help="fetch ground elevation (extra Elevation API call)")
    p_dl.add_argument("--parallel", type=int, default=10)
    p_dl.add_argument("--dry-run", action="store_true",
                      help="discover only; don't download GLBs")

    p_lu = sub.add_parser("landuse", help="OSM landuse → per-chunk category map "
                                          "(writes landuse.json next to a baked region)")
    p_lu.add_argument("--region", required=True,
                      help="path to baked region file (e.g. ~/.voxel/regions/toronto/blocks.bin)")
    p_lu.add_argument("--location", help='geocoded place; alternative to --lat/--lng')
    p_lu.add_argument("--lat", type=float)
    p_lu.add_argument("--lng", type=float)
    p_lu.add_argument("--chunk-size", type=int, default=16,
                      help="chunk side in blocks (must match engine CHUNK_SIZE)")

    p_w = sub.add_parser("world", help="end-to-end: geocode → bake-if-missing "
                                       "→ exec the engine. `make world` calls this.")
    p_w.add_argument("--location", help='geocoded place name; alternative to --lat/--lng')
    p_w.add_argument("--lat", type=float)
    p_w.add_argument("--lng", type=float)
    p_w.add_argument("--radius", type=float, default=800.0,
                     help="horizontal radius in metres (default 800)")
    p_w.add_argument("--height", type=float, default=0.0,
                     help="vertical extent in metres (default 2·radius)")
    p_w.add_argument("--elevation", action="store_true",
                     help="fetch ground elevation (extra Elevation API call)")
    p_w.add_argument("--parallel", type=int, default=10)
    p_w.add_argument("--voxel-size", type=float, default=1.0)
    p_w.add_argument("--build-dir", default="build-perf",
                     help="where solarium-ui-vk + solarium-voxel-bake live")
    p_w.add_argument("--template-index", type=int, default=7,
                     help="kWorldTemplates index for voxel_earth_dynamic.py")
    p_w.add_argument("--name", help="display name shown in HUD")
    p_w.add_argument("--force", action="store_true",
                     help="re-bake even if a cached region exists")

    args = p.parse_args(argv)
    if args.cmd == "download" and not args.location and (args.lat is None or args.lng is None):
        p.error("download: pass either --location or both --lat and --lng")
    if args.cmd == "landuse" and not args.location and (args.lat is None or args.lng is None):
        p.error("landuse: pass either --location or both --lat and --lng")
    if args.cmd == "world" and not args.location and (args.lat is None or args.lng is None):
        p.error("world: pass either --location or both --lat and --lng")
    return {
        "init":     cmd_init,
        "set-key":  cmd_set_key,
        "geocode":  cmd_geocode,
        "download": cmd_download,
        "landuse":  cmd_landuse,
        "world":    cmd_world,
    }[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

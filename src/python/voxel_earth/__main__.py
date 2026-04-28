"""CLI entry: python -m voxel_earth <command> ..."""
from __future__ import annotations

import argparse
import struct
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

    args = p.parse_args(argv)
    if args.cmd == "download" and not args.location and (args.lat is None or args.lng is None):
        p.error("download: pass either --location or both --lat and --lng")
    if args.cmd == "landuse" and not args.location and (args.lat is None or args.lng is None):
        p.error("landuse: pass either --location or both --lat and --lng")
    return {
        "init":     cmd_init,
        "set-key":  cmd_set_key,
        "geocode":  cmd_geocode,
        "download": cmd_download,
        "landuse":  cmd_landuse,
    }[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

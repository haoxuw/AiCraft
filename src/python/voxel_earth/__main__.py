"""CLI entry: python -m voxel_earth <command> ..."""
from __future__ import annotations

import argparse
import sys

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
        tiles = _dl.discover(cache.read_api_key(), lat, lng, args.radius,
                             elevation=elev, cache=cache)
    except ApiKeyMissing as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except GoogleApiError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"discovered {len(tiles)} leaf tiles in radius {args.radius}m")
    if args.dry_run or not tiles:
        return 0
    dl, skip = _dl.download_all(tiles, cache, parallel=args.parallel)
    print(f"downloaded {dl}, cache hits {skip}, total cached: "
          f"{len(list((cache.root / 'google' / 'glb').glob('*.glb')))}")
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
    p_dl.add_argument("--radius", type=float, default=200.0, help="meters (default 200)")
    p_dl.add_argument("--elevation", action="store_true",
                      help="fetch ground elevation (extra Elevation API call)")
    p_dl.add_argument("--parallel", type=int, default=10)
    p_dl.add_argument("--dry-run", action="store_true",
                      help="discover only; don't download GLBs")

    args = p.parse_args(argv)
    if args.cmd == "download" and not args.location and (args.lat is None or args.lng is None):
        p.error("download: pass either --location or both --lat and --lng")
    return {
        "init": cmd_init,
        "set-key": cmd_set_key,
        "geocode": cmd_geocode,
        "download": cmd_download,
    }[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())

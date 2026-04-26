"""cache.py — content-addressed cache under ~/.voxel/.

Layout
──────
  ~/.voxel/
    api_key                  plain-text Google Maps Platform key
    google/
      session.json           cached session token + expiry (3h TTL)
      glb/<obb-sha1>.glb     raw downloaded GLB, keyed by sha1 of bounding-box
                             (URLs aren't stable: Google bakes the session token
                             into the pathname under /files/, so URL-hashing
                             never dedups across sessions; OBB is stable.)
    discover/<lat>_<lng>_<r>.json   list of OBBs (stable across sessions). On a
                                     repeat `download` for the same region we
                                     check this first; if every listed OBB has
                                     a GLB on disk, we make ZERO API calls.
    elevation/<lat>_<lng>.json      cached Elevation API result.
    decoded/<sha1>_<origin-hash>.glb     Draco-decoded + rotated; origin baked in
    voxels/<sha1>_<origin>_<grid>.vox.bin   palette + xyzi, RLE-packed
    regions/<region-key>/                final per-world-region placements
      manifest.json
      blocks.bin
    geocode/<slug>.json      cached geocode results

Each filename includes everything that affects its content, so editing the
block palette only invalidates regions/, not voxels/ or glb/.
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse, parse_qsl, urlencode, urlunparse


def _default_root() -> Path:
    return Path(os.environ.get("VOXEL_CACHE", str(Path.home() / ".voxel")))


def url_sha1(url: str) -> str:
    """SHA1 of the URL with `key` and `session` stripped. Note: leaf .glb URLs
    embed the session token in their pathname (under /files/), so this only
    dedups within a single 3h session window."""
    p = urlparse(url)
    keep = [(k, v) for k, v in parse_qsl(p.query, keep_blank_values=True)
            if k not in ("key", "session")]
    canon = urlunparse(p._replace(query=urlencode(keep)))
    return hashlib.sha1(canon.encode("utf-8")).hexdigest()


def obb_sha1(box: list[float] | tuple[float, ...]) -> str:
    """Stable identity for a 3D Tiles leaf: sha1 of the 12-float bounding box
    rounded to 1mm. Same logical tile → same hash across sessions/days."""
    if len(box) != 12:
        raise ValueError(f"obb box must have 12 floats, got {len(box)}")
    canon = ",".join(f"{round(v, 3):.3f}" for v in box)
    return hashlib.sha1(canon.encode("utf-8")).hexdigest()


def slug(text: str) -> str:
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in text.strip().lower())[:80]


class VoxelCache:
    def __init__(self, root: Optional[Path] = None) -> None:
        self.root = Path(root) if root else _default_root()

    def init(self) -> None:
        for sub in ("google/glb", "decoded", "voxels", "regions", "geocode",
                    "discover", "elevation"):
            (self.root / sub).mkdir(parents=True, exist_ok=True)

    @property
    def api_key_path(self) -> Path:
        return self.root / "api_key"

    @property
    def session_path(self) -> Path:
        return self.root / "google" / "session.json"

    def glb_path(self, obb_box: list[float] | tuple[float, ...]) -> Path:
        return self.root / "google" / "glb" / f"{obb_sha1(obb_box)}.glb"

    def discover_path(self, lat: float, lng: float, radius: float) -> Path:
        # 6 decimals ≈ 11 cm at the equator — finer than Google's tile granularity.
        return self.root / "discover" / f"{lat:.6f}_{lng:.6f}_{int(round(radius))}.json"

    def elevation_path(self, lat: float, lng: float) -> Path:
        return self.root / "elevation" / f"{lat:.4f}_{lng:.4f}.json"

    def geocode_path(self, query: str) -> Path:
        return self.root / "geocode" / f"{slug(query)}.json"

    def read_api_key(self) -> Optional[str]:
        env = os.environ.get("VOXEL_EARTH_API_KEY")
        if env:
            return env.strip()
        if self.api_key_path.is_file():
            key = self.api_key_path.read_text(encoding="utf-8").strip()
            return key or None
        return None

    def write_api_key(self, key: str) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        self.api_key_path.write_text(key.strip(), encoding="utf-8")
        os.chmod(self.api_key_path, 0o600)

    def load_session(self) -> Optional[str]:
        if not self.session_path.is_file():
            return None
        try:
            data = json.loads(self.session_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        if data.get("expires_at_ms", 0) < int(time.time() * 1000):
            return None
        return data.get("session")

    def save_session(self, session: str, ttl_seconds: int = 3 * 3600) -> None:
        self.session_path.parent.mkdir(parents=True, exist_ok=True)
        self.session_path.write_text(
            json.dumps({"session": session,
                        "expires_at_ms": int((time.time() + ttl_seconds) * 1000)}),
            encoding="utf-8",
        )

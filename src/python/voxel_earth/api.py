"""api.py — minimal Google Maps Platform client.

Implements only what the pipeline needs:
  - geocode(text) → (lat, lng)
  - elevation(lat, lng) → meters  (optional, used by tile sphere centering)
  - 3D Tiles BFS lives in download.py (phase 2) — uses the same key.

Bring-your-own API key. Required SKUs on the project:
  - Geocoding API
  - Elevation API   (optional)
  - Map Tiles API   (Photorealistic 3D Tiles)
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
from typing import Optional, Tuple

from .cache import VoxelCache


class ApiKeyMissing(RuntimeError):
    pass


class GoogleApiError(RuntimeError):
    pass


class GoogleApi:
    def __init__(self, cache: VoxelCache, timeout: float = 10.0) -> None:
        self.cache = cache
        self.timeout = timeout

    def _key(self) -> str:
        key = self.cache.read_api_key()
        if not key:
            raise ApiKeyMissing(
                f"No Google API key. Set $VOXEL_EARTH_API_KEY or write one to "
                f"{self.cache.api_key_path}"
            )
        return key

    def _get_json(self, url: str) -> dict:
        with urllib.request.urlopen(url, timeout=self.timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def geocode(self, query: str, *, use_cache: bool = True) -> Tuple[float, float]:
        path = self.cache.geocode_path(query)
        if use_cache and path.is_file():
            data = json.loads(path.read_text(encoding="utf-8"))
        else:
            url = ("https://maps.googleapis.com/maps/api/geocode/json?"
                   + urllib.parse.urlencode({"address": query, "key": self._key()}))
            data = self._get_json(url)
            if data.get("status") != "OK" or not data.get("results"):
                raise GoogleApiError(f"Geocode failed for {query!r}: {data.get('status')}")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data, indent=2), encoding="utf-8")
        loc = data["results"][0]["geometry"]["location"]
        return float(loc["lat"]), float(loc["lng"])

    def elevation(self, lat: float, lng: float, *, use_cache: bool = True) -> float:
        path = self.cache.elevation_path(lat, lng)
        if use_cache and path.is_file():
            data = json.loads(path.read_text(encoding="utf-8"))
        else:
            url = ("https://maps.googleapis.com/maps/api/elevation/json?"
                   + urllib.parse.urlencode({"locations": f"{lat},{lng}", "key": self._key()}))
            data = self._get_json(url)
            if data.get("status") != "OK" or not data.get("results"):
                raise GoogleApiError(f"Elevation failed: {data.get('status')}")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data, indent=2), encoding="utf-8")
        return float(data["results"][0]["elevation"])

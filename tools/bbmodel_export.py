#!/usr/bin/env python3
"""Export a CivCraft Python model → Blockbench .bbmodel (JSON).

Geometry round-trips via bbmodel_import.py. Things Blockbench does not
model (per-part RGBA colors, sinusoidal clip parameters, hand/pivot
attachment points, head flag) are preserved elsewhere:
  - Colors are stashed in a `civcraft` custom field on each element so
    the importer can recover them. Blockbench itself ignores unknown
    fields but does round-trip them on save.
  - Clips, hand_r/l, pivot_r/l, height, scale, etc. are re-read from
    the original .py at import time. Modders who want to rearrange
    parts in Blockbench should keep part names stable so the importer
    can merge by name.

Usage:
    tools/bbmodel_export.py src/CivCraft/artifacts/models/base/player.py
    tools/bbmodel_export.py <model.py> <out.bbmodel>
"""

from __future__ import annotations

import json
import math
import runpy
import sys
import uuid
from pathlib import Path


def load_model_dict(py_path: Path) -> dict:
    """Execute the .py file and return its top-level `model` dict."""
    ns = runpy.run_path(str(py_path))
    if "model" not in ns:
        raise SystemExit(f"{py_path}: no top-level `model` dict")
    return ns["model"]


def rgba_to_int_color(rgba: list[float]) -> int:
    """Closest Blockbench marker color (0-7) to rgba — only a UI hint."""
    # Blockbench marker palette (from its source):
    #   0 red, 1 orange, 2 yellow, 3 green, 4 cyan, 5 blue, 6 magenta, 7 pink
    palette = [
        (0.92, 0.25, 0.23),  # red
        (0.95, 0.60, 0.20),  # orange
        (0.95, 0.82, 0.20),  # yellow
        (0.35, 0.75, 0.35),  # green
        (0.30, 0.70, 0.80),  # cyan
        (0.25, 0.45, 0.85),  # blue
        (0.70, 0.35, 0.80),  # magenta
        (0.90, 0.55, 0.75),  # pink
    ]
    r, g, b = rgba[0], rgba[1], rgba[2]
    best, best_d = 0, 1e9
    for i, (pr, pg, pb) in enumerate(palette):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if d < best_d:
            best, best_d = i, d
    return best


def part_to_element(idx: int, part: dict) -> dict:
    """Convert one CivCraft part dict to a Blockbench cuboid element."""
    offset = part["offset"]
    size = part["size"]
    # CivCraft size = full extents; Blockbench from/to = corner positions.
    half = [size[0] / 2, size[1] / 2, size[2] / 2]
    frm = [offset[0] - half[0], offset[1] - half[1], offset[2] - half[2]]
    to = [offset[0] + half[0], offset[1] + half[1], offset[2] + half[2]]
    # Origin = pivot if provided, else part center.
    origin = part.get("pivot", offset)

    name = part.get("name") or f"part_{idx}"

    # Preserve the full CivCraft part dict in a custom field so the
    # importer can recover color / swing / head flag verbatim if the
    # modder hasn't touched those. Blockbench round-trips unknown keys.
    civcraft_meta = {k: v for k, v in part.items() if k not in ("offset", "size", "pivot")}
    # Track whether the original .py omitted `pivot` so the importer can
    # preserve byte-identical round-trip for static parts that rely on the
    # C++ loader's (0,0,0) default.
    civcraft_meta["_had_pivot"] = "pivot" in part

    return {
        "name": name,
        "rescale": False,
        "locked": False,
        "light_emission": 0,
        "render_order": "default",
        "allow_mirror_modeling": True,
        "from": frm,
        "to": to,
        "autouv": 0,
        "color": rgba_to_int_color(part.get("color", [1, 1, 1, 1])),
        "origin": list(origin),
        "rotation": [0, 0, 0],
        "uv_offset": [0, 0],
        "faces": {
            face: {"uv": [0, 0, 4, 4], "texture": None}
            for face in ("north", "east", "south", "west", "up", "down")
        },
        "type": "cube",
        "uuid": str(uuid.uuid4()),
        # Non-Blockbench extension — preserved on round-trip:
        "civcraft": civcraft_meta,
    }


def export(py_path: Path, out_path: Path) -> None:
    model = load_model_dict(py_path)
    parts = model.get("parts", [])
    elements = [part_to_element(i, p) for i, p in enumerate(parts)]
    uuids = [e["uuid"] for e in elements]

    bb = {
        "meta": {
            "format_version": "4.5",
            "model_format": "free",
            "box_uv": False,
            "creation_time": 0,
        },
        "name": model.get("id", py_path.stem),
        "model_identifier": "",
        "visible_box": [1, 1, 0],
        "variable_placeholders": "",
        "variable_placeholder_buttons": [],
        "timeline_setups": [],
        "unhandled_root_fields": {},
        "resolution": {"width": 16, "height": 16},
        "elements": elements,
        "outliner": uuids,  # flat — one group would need extra UUIDs
        "textures": [],
        "animations": [],
        # Top-level extension: everything we can't put in elements.
        "civcraft": {
            "id": model.get("id"),
            "height": model.get("height"),
            "scale": model.get("scale"),
            "walk_speed": model.get("walk_speed"),
            "idle_bob": model.get("idle_bob"),
            "walk_bob": model.get("walk_bob"),
            "hand_r": model.get("hand_r"),
            "hand_l": model.get("hand_l"),
            "pivot_r": model.get("pivot_r"),
            "pivot_l": model.get("pivot_l"),
            "head_pivot": model.get("head_pivot"),
            "clips": model.get("clips"),
            "source_file": str(py_path),
        },
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w") as f:
        json.dump(bb, f, indent=2, default=_json_default)
    print(f"[bbmodel_export] {py_path} → {out_path} ({len(elements)} cuboids)")


def _json_default(x):
    # math.pi etc. come through as plain floats already, but guard against
    # any unexpected types that may slip in (numpy floats etc.).
    if isinstance(x, float):
        return x
    if hasattr(x, "tolist"):
        return x.tolist()
    raise TypeError(f"not JSON-serializable: {type(x).__name__}")


def main(argv: list[str]) -> int:
    if len(argv) < 2 or len(argv) > 3:
        print("usage: bbmodel_export.py <model.py> [out.bbmodel]", file=sys.stderr)
        return 2
    py_path = Path(argv[1]).resolve()
    if not py_path.is_file():
        print(f"{py_path}: not a file", file=sys.stderr)
        return 1
    if len(argv) == 3:
        out_path = Path(argv[2]).resolve()
    else:
        out_path = py_path.with_suffix(".bbmodel")
    export(py_path, out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

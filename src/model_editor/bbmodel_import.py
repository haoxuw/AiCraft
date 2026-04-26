#!/usr/bin/env python3
"""Import a Blockbench .bbmodel back into a Solarium Python model.

Merges the geometry edited in Blockbench (part positions, sizes, pivots,
added/removed parts) with the original .py's non-Blockbench fields
(colors, swing params, clips, hand/pivot attachment points, head flag).

Merge strategy, in order:
  1. For each .bbmodel element: if its name matches a part in the
     original .py, we rebuild that part from the .bbmodel geometry
     but keep the original part's color / swing_axis / amplitude /
     phase / speed / head flag.
  2. If the element has our `solarium` extension dict (written by
     bbmodel_export.py), we read color/swing/head from there when
     there is no name-match in the original .py — useful for parts
     the modder duplicated or renamed.
  3. Otherwise the part gets default color [0.7, 0.7, 0.7, 1] and
     no swing — modder must fill in from the Properties panel.

Part ordering comes from the .bbmodel's `outliner` (Blockbench's
editing order) — which is also the draw order in-game. Modders who
reorder in Blockbench will see that reorder reflected.

Usage:
    src/model_editor/bbmodel_import.py <in.bbmodel> <out.py>
    src/model_editor/bbmodel_import.py <in.bbmodel> <out.py> --base <original.py>

If --base is omitted, the importer looks for a .py at the path recorded
in the bbmodel's solarium.source_file field.
"""

from __future__ import annotations

import argparse
import json
import math
import runpy
import sys
from pathlib import Path


def load_base_model(path: Path | None) -> dict | None:
    if path is None or not path.is_file():
        return None
    ns = runpy.run_path(str(path))
    return ns.get("model")


def element_to_part(elem: dict, base_by_name: dict) -> dict:
    """Rebuild one Solarium part from a Blockbench element."""
    frm = elem["from"]
    to = elem["to"]
    # Blockbench from/to are corners; we want center + full size.
    offset = [(frm[0] + to[0]) / 2, (frm[1] + to[1]) / 2, (frm[2] + to[2]) / 2]
    size = [to[0] - frm[0], to[1] - frm[1], to[2] - frm[2]]
    origin = elem.get("origin", offset)

    name = elem.get("name")
    part: dict = {"offset": offset, "size": size}
    if name and not name.startswith("part_"):
        part["name"] = name

    # Layer color / swing params on top from the best available source.
    preserved_keys = (
        "color", "swing_axis", "amplitude", "phase", "speed", "head", "role",
    )
    base_part = base_by_name.get(name) if name else None
    solarium_meta = elem.get("solarium") or {}

    # Emit pivot unless the original .py explicitly omitted it (and no
    # base match gives us one) — the C++ loader defaults absent pivot to
    # (0,0,0), which is semantically distinct from pivot=offset.
    had_pivot = solarium_meta.get("_had_pivot")
    base_had_pivot = base_part and "pivot" in base_part
    if had_pivot is None or had_pivot or base_had_pivot:
        part["pivot"] = list(base_part["pivot"]) if base_had_pivot else list(origin)

    for key in preserved_keys:
        if base_part and key in base_part:
            part[key] = base_part[key]
        elif key in solarium_meta:
            part[key] = solarium_meta[key]

    # Fallback color so a brand-new part isn't invisible.
    if "color" not in part:
        part["color"] = [0.7, 0.7, 0.7, 1]

    return part


def format_value(v, indent: int = 0) -> str:
    """Format a Python value for emission into the generated .py."""
    pad = " " * indent
    if isinstance(v, bool):
        return "True" if v else "False"
    if isinstance(v, (int, float)):
        if isinstance(v, float):
            # Round-trip math.pi and multiples cleanly so diffs stay readable.
            for mult in (1, -1, 0.5, -0.5, 2, -2):
                if abs(v - math.pi * mult) < 1e-9:
                    return ("math.pi" if mult == 1 else
                            "-math.pi" if mult == -1 else
                            f"math.pi * {mult}" if mult > 0 else
                            f"-math.pi * {abs(mult)}")
            # Otherwise emit with enough precision to round-trip.
            if v == int(v):
                return f"{int(v)}"
            return f"{v:.6g}"
        return str(v)
    if isinstance(v, str):
        return json.dumps(v)
    if isinstance(v, list):
        return "[" + ", ".join(format_value(x, indent) for x in v) + "]"
    if isinstance(v, dict):
        if not v:
            return "{}"
        inner = [f'{pad}    "{k}": {format_value(val, indent + 4)}' for k, val in v.items()]
        return "{\n" + ",\n".join(inner) + f"\n{pad}}}"
    raise TypeError(f"cannot format {type(v).__name__}")


def emit_part(part: dict) -> str:
    """Emit one part dict as a compact one-liner if it fits, else multiline."""
    # Keep the canonical key order so diffs against the original .py
    # stay small for unchanged parts.
    order = ["name", "head", "role", "offset", "size", "color",
             "pivot", "swing_axis", "amplitude", "phase", "speed"]
    keys = [k for k in order if k in part] + [k for k in part if k not in order]
    pieces = [f'"{k}": {format_value(part[k])}' for k in keys]
    line = "{" + ", ".join(pieces) + "}"
    if len(line) <= 110:
        return line
    return "{\n        " + ",\n        ".join(pieces) + "\n    }"


def emit_model(model: dict) -> str:
    """Serialize the assembled model dict as a .py file."""
    lines = [
        '"""Auto-generated from .bbmodel via src/model_editor/bbmodel_import.py.',
        "",
        "Edit geometry in Blockbench; run bbmodel_import.py to sync back.",
        "Non-geometry fields (clips, hand/pivot, swing params) are preserved",
        "from the previous .py via name-match merge.",
        '"""',
        "",
        "import math",
        "",
        "model = {",
    ]
    # Top-level scalar fields in a fixed order.
    for k in ("id", "height", "scale", "walk_speed", "idle_bob", "walk_bob",
              "hand_r", "hand_l", "pivot_r", "pivot_l", "head_pivot"):
        if k in model and model[k] is not None:
            lines.append(f'    "{k}": {format_value(model[k], 4)},')
    # Parts.
    lines.append('    "parts": [')
    for part in model.get("parts", []):
        lines.append("        " + emit_part(part) + ",")
    lines.append("    ],")
    # Clips (preserved verbatim from the original).
    if model.get("clips"):
        lines.append(f'    "clips": {format_value(model["clips"], 4)},')
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def import_bb(bb_path: Path, out_path: Path, base_path: Path | None) -> None:
    with bb_path.open() as f:
        bb = json.load(f)

    # If no explicit base was passed, try the source path we stamped on export.
    if base_path is None:
        recorded = (bb.get("solarium") or {}).get("source_file")
        if recorded:
            cand = Path(recorded)
            if cand.is_file():
                base_path = cand

    base_model = load_base_model(base_path) or {}
    base_parts = base_model.get("parts", [])
    base_by_name = {p["name"]: p for p in base_parts if "name" in p}

    elements = bb.get("elements", [])
    uuid_to_elem = {e["uuid"]: e for e in elements}
    order = bb.get("outliner") or [e["uuid"] for e in elements]
    # outliner may contain group dicts; flatten to plain uuids.
    flat = []
    def walk(node):
        if isinstance(node, str):
            if node in uuid_to_elem: flat.append(node)
        elif isinstance(node, dict):
            for child in node.get("children", []): walk(child)
    for n in order:
        walk(n)
    # Fallback: any elements not reached via outliner, append in declaration order.
    seen = set(flat)
    for e in elements:
        if e["uuid"] not in seen:
            flat.append(e["uuid"])

    new_parts = [element_to_part(uuid_to_elem[u], base_by_name) for u in flat]

    bb_solarium = bb.get("solarium") or {}
    out_model: dict = {}
    for k in ("id", "height", "scale", "walk_speed", "idle_bob", "walk_bob",
              "hand_r", "hand_l", "pivot_r", "pivot_l", "head_pivot"):
        # Prefer the base (hand-authored) value. Fall back to the bbmodel's
        # stash. Never write None.
        if k in base_model and base_model[k] is not None:
            out_model[k] = base_model[k]
        elif bb_solarium.get(k) is not None:
            out_model[k] = bb_solarium[k]

    out_model["parts"] = new_parts
    if base_model.get("clips"):
        out_model["clips"] = base_model["clips"]
    elif bb_solarium.get("clips"):
        out_model["clips"] = bb_solarium["clips"]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(emit_model(out_model))
    print(f"[bbmodel_import] {bb_path} → {out_path} "
          f"({len(new_parts)} parts, base={base_path})")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("bbmodel")
    ap.add_argument("output_py")
    ap.add_argument("--base", default=None,
                    help="Original .py to merge non-geometry fields from")
    args = ap.parse_args(argv[1:])

    bb_path = Path(args.bbmodel).resolve()
    out_path = Path(args.output_py).resolve()
    base_path = Path(args.base).resolve() if args.base else None
    if not bb_path.is_file():
        print(f"{bb_path}: not a file", file=sys.stderr); return 1

    import_bb(bb_path, out_path, base_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

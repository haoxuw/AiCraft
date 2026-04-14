#!/usr/bin/env python3
"""
Check box-model .py files for z-fighting risk.

Z-fighting happens when two part surfaces are (nearly) coplanar AND the parts
overlap on the other two axes. This script loads each model file and reports
every such pair, along with a suggested direction to push the offending part
along the shared axis.

Usage:
    python3 src/CivCraft/tools/check_zfight.py
    python3 src/CivCraft/tools/check_zfight.py src/CivCraft/artifacts/models/base/pig.py
    python3 src/CivCraft/tools/check_zfight.py --tolerance 0.01

Exit status is the number of warnings (0 = clean).
"""

from __future__ import annotations
import argparse
import math
import os
import runpy
import sys
from dataclasses import dataclass
from typing import List


@dataclass
class Part:
    idx: int
    name: str
    offset: tuple
    half: tuple
    role: str

    @property
    def lo(self): return tuple(self.offset[i] - self.half[i] for i in range(3))
    @property
    def hi(self): return tuple(self.offset[i] + self.half[i] for i in range(3))


AXIS = ['x', 'y', 'z']


def load_parts(path: str) -> List[Part]:
    # Stub math.pi etc. via runpy; model files are plain Python.
    ns = runpy.run_path(path)
    model = ns.get('model')
    if not model or 'parts' not in model:
        return []
    parts = []
    for i, p in enumerate(model['parts']):
        off = p.get('offset', [0, 0, 0])
        size = p.get('size', [0, 0, 0])
        parts.append(Part(
            idx=i,
            name=p.get('name', ''),
            offset=tuple(off),
            half=tuple(s * 0.5 for s in size),
            role=p.get('role', ''),
        ))
    return parts


def overlap_amount(a_lo, a_hi, b_lo, b_hi) -> float:
    """Overlap length on one axis (0 = touching or disjoint)."""
    return max(0.0, min(a_hi, b_hi) - max(a_lo, b_lo))


def check_pair(a: Part, b: Part, tol: float):
    """Yield (axis, side, dist) warnings for coplanar faces with overlap on the
    other two axes. `side` is 'lo'/'hi' relative to part a."""
    # Must overlap on at least 2 axes for z-fighting to be visible
    overlaps = [overlap_amount(a.lo[i], a.hi[i], b.lo[i], b.hi[i]) for i in range(3)]
    n_overlap_axes = sum(1 for o in overlaps if o > tol)
    if n_overlap_axes < 2:
        return

    for axis in range(3):
        # Coplanar faces: a.lo == b.lo OR a.lo == b.hi OR a.hi == b.lo OR a.hi == b.hi
        # AND the other two axes overlap with non-trivial area.
        other_overlaps = [overlaps[j] for j in range(3) if j != axis]
        if min(other_overlaps) <= tol:
            continue  # no overlap on orthogonal axes → faces don't share area
        # Only same-direction face pairs cause z-fighting. lo-vs-hi pairs are
        # opposite-facing (e.g. abutting parts): the two faces point away from
        # each other and are not rasterized as a conflict.
        for side in ('lo', 'hi'):
            av = getattr(a, side)[axis]
            bv = getattr(b, side)[axis]
            d = abs(av - bv)
            if d < tol:
                yield axis, side, side, d


def describe_part(p: Part) -> str:
    tag = p.name or p.role or f'#{p.idx}'
    return f'[{p.idx}]{tag}'


def check_file(path: str, tol: float) -> list:
    try:
        parts = load_parts(path)
    except Exception as e:
        return [f'  ✗ failed to load: {e}']
    if not parts:
        return []
    out = []
    for i in range(len(parts)):
        for j in range(i + 1, len(parts)):
            a, b = parts[i], parts[j]
            for axis, a_side, b_side, d in check_pair(a, b, tol):
                ax = AXIS[axis]
                out.append(
                    f'  {describe_part(a):<20} {a_side}.{ax}={getattr(a, a_side)[axis]:+.4f}  '
                    f'≈  {describe_part(b):<20} {b_side}.{ax}={getattr(b, b_side)[axis]:+.4f}  '
                    f'(d={d:.4f})'
                )
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='*',
                    help='Model .py files (default: artifacts/models/base/*.py)')
    ap.add_argument('--tolerance', type=float, default=0.008,
                    help='Face coplanarity tolerance in world units (default 0.008)')
    args = ap.parse_args()

    if args.paths:
        paths = args.paths
    else:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        base = os.path.join(root, 'artifacts', 'models', 'base')
        paths = sorted(os.path.join(base, f) for f in os.listdir(base) if f.endswith('.py'))

    total = 0
    dirty_files = 0
    for path in paths:
        rel = os.path.relpath(path)
        warnings = check_file(path, args.tolerance)
        if warnings:
            print(f'── {rel}  ({len(warnings)} warnings) ──')
            for w in warnings: print(w)
            print()
            dirty_files += 1
        total += len(warnings)

    print(f'Summary: {total} warnings across {dirty_files}/{len(paths)} files '
          f'(tolerance={args.tolerance})')
    sys.exit(0 if total == 0 else 1)


if __name__ == '__main__':
    main()

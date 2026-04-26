#!/usr/bin/env python3
"""ModelCrafter — standalone animation debugger.

Loads a model .py directly (no build step), runs the same clip math as
src/platform/client/model.cpp, and renders boxes in matplotlib 3D.

Usage:
  # Interactive — time scrubs automatically, hit clip buttons to switch:
  python3 src/model_editor/modelcrafter.py src/artifacts/models/base/guy.py

  # Force a single clip:
  python3 src/model_editor/modelcrafter.py <model.py> --clip attack

  # Headless snapshot at a specific time (useful for /tmp sweeps):
  python3 src/model_editor/modelcrafter.py <model.py> --clip sit --snapshot /tmp/sit.png \\
      --time 0.5

Clip evaluation (mirrors model.cpp):
  angle = sin(t * speed * 2π + phase) * amp + bias
  applied as rotation around `axis`, pivoted at `pivot`.
  For named-clip overrides, per-part clip dicts replace swing_axis/amplitude/
  bias/speed/phase of the base part.

Limitations (intentional — keep the tool under ~300 lines):
  * No held-item rendering.
  * No walk cycle (clips with speed=0 still animate via the sin driver).
  * Attack combo clips (swing_left/right, cleave) live in attack_anim.h as
    C++ keyframe data, not in the Python model — they aren't visible here.
"""
from __future__ import annotations

import argparse
import importlib.util
import math
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider, RadioButtons, Button, CheckButtons
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

# Combo keyframe clips (mirror of attack_anim.h). Loaded lazily so snapshot
# mode without the combo-aware path still has zero overhead.
import importlib.util as _ilu
_combo_spec = _ilu.spec_from_file_location(
    "attack_clips", Path(__file__).parent / "attack_clips.py")
_combo_mod = _ilu.module_from_spec(_combo_spec)
_combo_spec.loader.exec_module(_combo_mod)
ATTACK_CLIPS = _combo_mod.ATTACK_CLIPS
sample_arm   = _combo_mod.sample_arm


# ── Model discovery ────────────────────────────────────────────────────────

MODEL_GLOBS = (
    "src/artifacts/models/**/*.py",
    "src/artifacts/models/*.py",
    # Legacy per-game layout (kept so checkouts mid-migration still work).
    "src/*/artifacts/models/**/*.py",
)


def discover_models(repo_root: Path) -> list[Path]:
    """Every model .py under src/artifacts/models/ (or legacy per-game), sorted by id."""
    seen: set[Path] = set()
    out: list[Path] = []
    for pat in MODEL_GLOBS:
        for p in sorted(repo_root.glob(pat)):
            if p.name.startswith("_") or "__pycache__" in p.parts:
                continue
            rp = p.resolve()
            if rp in seen:
                continue
            seen.add(rp)
            out.append(p)
    return out


# ── Model loader ────────────────────────────────────────────────────────────

def load_model(path: Path) -> dict:
    """Exec the model .py in isolation and return its `model` dict.

    We use importlib so relative imports inside the file (e.g. `import math`)
    work exactly as they would for the game's Python loader.
    """
    spec = importlib.util.spec_from_file_location("model_under_debug", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if not hasattr(mod, "model"):
        raise RuntimeError(f"{path}: no `model` dict found")
    return mod.model


# ── Part transform ─────────────────────────────────────────────────────────

def apply_equip_rotation(points: np.ndarray, deg: list[float]) -> np.ndarray:
    """Apply the equip rotation in the same order as model.cpp.

    The game does: root *= Ry * Rx * Rz, which means vectors flow through
    Rz first, then Rx, then Ry. So the combined matrix applied to a point
    is R = Ry @ Rx @ Rz. The naming `rotation=[x,y,z]` is just component
    labels; the ORDER of application is Z→X→Y.
    """
    x, y, z = [math.radians(d) for d in deg]
    cx, sx = math.cos(x), math.sin(x)
    cy, sy = math.cos(y), math.sin(y)
    cz, sz = math.cos(z), math.sin(z)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    R = Ry @ Rx @ Rz
    return points @ R.T


def rotate_around(points: np.ndarray, axis: np.ndarray, angle_rad: float,
                  pivot: np.ndarray) -> np.ndarray:
    """Rodrigues rotation of `points` (N×3) around `axis` through `pivot`."""
    if abs(angle_rad) < 1e-9:
        return points
    k = axis / (np.linalg.norm(axis) + 1e-12)
    c, s = math.cos(angle_rad), math.sin(angle_rad)
    # Shift to pivot-origin, rotate, shift back
    p = points - pivot
    rotated = (p * c
               + np.cross(k, p) * s
               + k * (p @ k)[:, None] * (1 - c))
    return rotated + pivot


def box_corners(offset: list[float], size: list[float]) -> np.ndarray:
    """8 corners of an axis-aligned box given center + full size."""
    o = np.array(offset, dtype=float)
    h = np.array(size, dtype=float) / 2.0
    signs = np.array([[sx, sy, sz]
                      for sx in (-1, 1)
                      for sy in (-1, 1)
                      for sz in (-1, 1)], dtype=float)
    return o + signs * h


# The 6 faces of a box given the 8 corners in the order produced by
# box_corners (binary-ordered by sx,sy,sz). Indices chosen so each face's
# vertex winding is consistent (doesn't matter for Poly3DCollection, but
# keeps things sane if this tool grows).
BOX_FACES = [
    (0, 1, 3, 2),  # -X
    (4, 5, 7, 6),  # +X
    (0, 1, 5, 4),  # -Y
    (2, 3, 7, 6),  # +Y
    (0, 2, 6, 4),  # -Z
    (1, 3, 7, 5),  # +Z
]


def effective_swing(part: dict, clip_overrides: dict | None) -> dict:
    """Merge a part's base swing params with a clip override (if any).

    model.cpp's behavior: a clip entry replaces axis/amp/bias/speed/phase on
    the part; pivot stays from the base part. We match that.
    """
    base = {
        "axis":  part.get("swing_axis", [1, 0, 0]),
        "amp":   part.get("amplitude", 0.0),
        "bias":  part.get("bias", 0.0),
        "speed": part.get("speed", 1.0),
        "phase": part.get("phase", 0.0),
        "pivot": part.get("pivot"),
    }
    if clip_overrides is None:
        return base
    ov = clip_overrides
    for key in ("axis", "amp", "bias", "speed", "phase"):
        if key in ov:
            base[key] = ov[key]
    return base


def transform_part(part: dict, t: float, clip_name: str,
                   model_clips: dict) -> np.ndarray:
    """Return the 8 world-space corners of `part` at time `t`.

    `clip_name` empty = idle (use base part swing_axis/amplitude). A named
    clip's overrides (if the part is listed) replace those. Parts not in the
    clip fall back to base swing — same as model.cpp.
    """
    corners = box_corners(part["offset"], part["size"])

    swing = effective_swing(
        part,
        (model_clips.get(clip_name, {}).overrides  # populated below
         if clip_name and clip_name in model_clips
         else None),
    )
    if swing["pivot"] is None or abs(swing["amp"]) + abs(swing["bias"]) < 1e-9:
        return corners

    angle = (math.sin(t * swing["speed"] * 2 * math.pi + swing["phase"])
             * math.radians(swing["amp"])
             + math.radians(swing["bias"]))
    return rotate_around(corners, np.array(swing["axis"], dtype=float),
                         angle, np.array(swing["pivot"], dtype=float))


# Lightweight holder so `model_clips[name].overrides` reads cleanly above.
class _Clip:
    def __init__(self, overrides: dict):
        self.overrides = overrides


def build_clip_names(model: dict, sin_clips: dict) -> list[str]:
    """(idle) + model's sinusoidal clips + combo keyframe clips (when
    applicable). Combo clips only apply to humanoid models — gated on the
    presence of a `right_hand`-named part AND a `pivot_r` attachment.
    """
    names = ["(idle)"] + sorted(sin_clips.keys())
    has_arm = any(p.get("name") == "right_hand" for p in model.get("parts", []))
    if has_arm and model.get("pivot_r") is not None:
        # Label combo entries so they don't collide with sin clips of the
        # same name (there currently aren't any, but future-proof).
        for c in ATTACK_CLIPS.keys():
            names.append(f"combo:{c}")
    return names


def build_clips(model: dict) -> dict[str, _Clip]:
    """{ clip_name: _Clip(per_part_overrides) } for every named clip."""
    out = {}
    for name, entry in (model.get("clips") or {}).items():
        # entry is { part_name: {axis, amp, bias, speed, phase} }
        per_part = {}
        for pname, ov in entry.items():
            per_part[pname] = ov
        # _Clip holds a combined overrides dict keyed by part name; our
        # effective_swing() path indexes by part name. Store that directly:
        c = _Clip({})
        c.overrides = per_part
        out[name] = c
    return out


def swing_for_part(part: dict, clip: _Clip | None) -> dict:
    """Resolve which override applies to this specific part under this clip."""
    if clip is None:
        return effective_swing(part, None)
    name = part.get("name")
    ov = clip.overrides.get(name) if name else None
    return effective_swing(part, ov)


def make_target_dummy(distance: float = 0.75) -> tuple[list[dict], list[np.ndarray]]:
    """Simple straw-man target in front of the character (faces -Z).

    Three boxes: head, torso post, base — sized roughly to a humanoid so
    swing reach is visually obvious. Returns (parts, corners) matching the
    same shape as held_item_corners() so draw_model can consume them.
    """
    z = -distance
    # Saturated greens so the dummy reads as distinct silhouette against the
    # character (which has tan/blue tones) — the red target circle keeps
    # the "aim here" focal point obvious.
    dummy = [
        # Base pad (dark wood)
        {"offset": [0, 0.05, z], "size": [0.70, 0.10, 0.70],
         "color": [0.22, 0.16, 0.08, 1]},
        # Torso post (green straw)
        {"offset": [0, 0.95, z], "size": [0.55, 1.60, 0.35],
         "color": [0.35, 0.55, 0.25, 1]},
        # Head
        {"offset": [0, 1.95, z], "size": [0.45, 0.40, 0.40],
         "color": [0.30, 0.48, 0.22, 1]},
        # Crossbar "arms" on the post (darker wood)
        {"offset": [0, 1.35, z], "size": [1.20, 0.10, 0.10],
         "color": [0.32, 0.22, 0.10, 1]},
        # Target circle (bright red — aim point)
        {"offset": [0, 1.10, z - 0.185], "size": [0.30, 0.30, 0.02],
         "color": [0.88, 0.12, 0.12, 1]},
    ]
    corners = [box_corners(p["offset"], p["size"]) for p in dummy]
    return dummy, corners


def held_item_corners(item_model: dict,
                      hand_pos: list[float],
                      combo_clip: str,
                      combo_pivot: list[float] | None,
                      t: float) -> tuple[list[dict], list[np.ndarray]]:
    """Transform a held item's parts into world space on the right hand.

    Pipeline mirrors what the C++ render path does for a held item:
      1. scale part around item-local origin
      2. apply equip rotation (Euler X→Y→Z)
      3. apply equip offset
      4. translate to hand_pos (world attachment)
      5. if a combo clip is active, rotate with the arm: pitch around X,
         then yaw around Y, at combo_pivot — same as the arm itself
    Returns (parts_for_color_lookup, corners_list).
    """
    equip = item_model.get("equip", {})
    scale = float(equip.get("scale", 1.0))
    rot   = equip.get("rotation", [0, 0, 0])
    off   = equip.get("offset", [0, 0, 0])
    off_v = np.array(off, dtype=float)
    hand_v = np.array(hand_pos, dtype=float)

    arm_pitch = arm_yaw = arm_roll = 0.0
    if combo_clip:
        s = sample_arm(combo_clip, t)
        if s is not None:
            arm_pitch, arm_yaw, arm_roll = s
    pivot_v = (np.array(combo_pivot, dtype=float)
               if combo_pivot is not None else None)

    # For wrist roll we need the CURRENT forearm axis (shoulder→hand after
    # pitch/yaw) and the CURRENT hand position (roll pivot). Compute once
    # per call — all parts share the same wrist frame.
    roll_axis = None
    roll_pivot = None
    if combo_clip and pivot_v is not None and abs(arm_roll) > 1e-6:
        hand_post = hand_v.copy()
        if abs(arm_pitch) > 1e-6:
            hand_post = rotate_around(
                hand_post.reshape(1, 3),
                np.array([1, 0, 0], dtype=float),
                math.radians(arm_pitch), pivot_v)[0]
        if abs(arm_yaw) > 1e-6:
            hand_post = rotate_around(
                hand_post.reshape(1, 3),
                np.array([0, 1, 0], dtype=float),
                math.radians(arm_yaw), pivot_v)[0]
        axis = hand_post - pivot_v
        n = np.linalg.norm(axis)
        if n > 1e-9:
            roll_axis = axis / n
            roll_pivot = hand_post

    out_corners = []
    parts = item_model.get("parts", [])
    for part in parts:
        c = box_corners(part["offset"], part["size"]) * scale
        c = apply_equip_rotation(c, rot)
        c = c + off_v + hand_v
        if combo_clip and pivot_v is not None:
            if abs(arm_pitch) > 1e-6:
                c = rotate_around(c, np.array([1, 0, 0], dtype=float),
                                  math.radians(arm_pitch), pivot_v)
            if abs(arm_yaw) > 1e-6:
                c = rotate_around(c, np.array([0, 1, 0], dtype=float),
                                  math.radians(arm_yaw), pivot_v)
            # Wrist roll — rotates sword around the *current* forearm axis
            # at the *current* hand position, AFTER arm pitch+yaw. Perpen-
            # dicularity with the arm is preserved (rotation around the arm
            # axis is the only DOF that can re-orient the blade without
            # breaking the 90° rest-offset).
            if roll_axis is not None:
                c = rotate_around(c, roll_axis,
                                  math.radians(arm_roll), roll_pivot)
        out_corners.append(c)
    return parts, out_corners


def transform_all(parts: list[dict], t: float,
                  clip: _Clip | None,
                  walk_cycle: bool = False,
                  combo_clip: str = "",
                  combo_pivot: list[float] | None = None) -> list[np.ndarray]:
    """Compute world corners for every part. Returns list of (8,3) arrays.

    Matches model.cpp's gating: walk-cycle base swings only fire when moving
    (smoothSpeed > 0). In this tool:
      • named clip override, if part is listed in the active clip
      • walk-cycle base swing, if walk_cycle=True and no override wins
      • rest pose, otherwise
    """
    out = []
    for part in parts:
        corners = box_corners(part["offset"], part["size"])

        name = part.get("name")
        ov = (clip.overrides.get(name) if (clip and name) else None)

        axis = amp = bias = speed = phase = None
        if ov is not None and part.get("pivot") is not None:
            axis  = ov.get("axis",  [1, 0, 0])
            amp   = ov.get("amp",   0.0)
            bias  = ov.get("bias",  0.0)
            speed = ov.get("speed", 1.0)
            phase = ov.get("phase", 0.0)
        elif walk_cycle and part.get("pivot") is not None \
                and part.get("amplitude") is not None:
            axis  = part.get("swing_axis", [1, 0, 0])
            amp   = part.get("amplitude", 0.0)
            bias  = part.get("bias", 0.0)
            speed = part.get("speed", 1.0)
            phase = part.get("phase", 0.0)

        if axis is not None and (abs(amp) > 1e-9 or abs(bias) > 1e-9):
            angle = (math.sin(t * speed * 2 * math.pi + phase)
                     * math.radians(amp)
                     + math.radians(bias))
            corners = rotate_around(
                corners, np.array(axis, dtype=float), angle,
                np.array(part["pivot"], dtype=float),
            )

        # Combo keyframe clips rotate the right arm around pivot_r. Pitch is
        # applied first (X axis), then yaw (Y axis) — matches the order the
        # game's AttackAnimPlayer feeds into model.cpp.
        if combo_clip and name == "right_hand" and combo_pivot is not None:
            sample = sample_arm(combo_clip, t)
            if sample is not None:
                pitch_deg, yaw_deg, _roll_deg = sample
                pivot = np.array(combo_pivot, dtype=float)
                if abs(pitch_deg) > 1e-6:
                    corners = rotate_around(
                        corners, np.array([1, 0, 0], dtype=float),
                        math.radians(pitch_deg), pivot)
                if abs(yaw_deg) > 1e-6:
                    corners = rotate_around(
                        corners, np.array([0, 1, 0], dtype=float),
                        math.radians(yaw_deg), pivot)
        out.append(corners)
    return out


# ── Rendering ──────────────────────────────────────────────────────────────

def draw_model(ax, parts: list[dict], corners_list: list[np.ndarray],
               show_ground: bool = True,
               extra_parts: list[dict] | None = None,
               extra_corners: list[np.ndarray] | None = None) -> None:
    ax.clear()
    all_parts = list(parts) + (list(extra_parts) if extra_parts else [])
    all_corners = list(corners_list) + (list(extra_corners) if extra_corners else [])
    for part, corners in zip(all_parts, all_corners):
        color = part.get("color", [0.6, 0.6, 0.65, 1])
        if len(color) == 3:
            color = (*color, 1.0)
        faces = [corners[list(idx)] for idx in BOX_FACES]
        coll = Poly3DCollection(faces, facecolor=color,
                                edgecolor=(0.1, 0.1, 0.1, 0.6), linewidth=0.4)
        ax.add_collection3d(coll)

    # Frame tightly around the model, but with comfy padding so heads and
    # sword tips don't clip against the axes box or the figure title.
    all_pts = np.vstack(all_corners)
    mn, mx = all_pts.min(0), all_pts.max(0)
    center = (mn + mx) / 2
    reach = (mx - mn).max() / 2 * 1.35

    # After remap_xyz: column 1 = model Z (forward/back, shown on mpl Y),
    # column 2 = model Y (height, shown on mpl Z). Keep the ground visible
    # even when the character has no parts at Y=0.
    zmin = min(center[2] - reach, 0.0)
    zmax = max(center[2] + reach, zmin + 2 * reach)

    # Use the largest half-span across all three axes so the cube stays
    # isotropic AND nothing gets clipped. Matplotlib's box_aspect can hide
    # geometry outside the smaller ranges even though set_*lim accepted
    # them, so we unify the spans ourselves.
    span = max(reach, (zmax - zmin) / 2)
    cx = center[0]; cy = center[1]; cz = (zmin + zmax) / 2
    ax.set_xlim(cx - span, cx + span)
    ax.set_ylim(cy - span, cy + span)                  # matplotlib Y ← model Z
    ax.set_zlim(cz - span, cz + span)                  # matplotlib Z ← model Y
    ax.set_xlabel("X"); ax.set_ylabel("Z"); ax.set_zlabel("Y")
    ax.set_box_aspect([1, 1, 1])

    # Turn off matplotlib's per-axis autoscaling — otherwise adding the
    # ground plot_surface below can silently retract our explicit limits
    # (the dummy at model z=-1.6 got clipped to z∈[-1,1.5] that way).
    ax.set_autoscale_on(False)
    ax.autoscale(False)

    if show_ground:
        # Faint grid square at matplotlib Z=0 (model Y=0) for height reference.
        gx = np.array([cx - span, cx + span])
        gy = np.array([cy - span, cy + span])
        xx, yy = np.meshgrid(gx, gy)
        zz = np.zeros_like(xx)
        ax.plot_surface(xx, yy, zz, color=(0.55, 0.55, 0.60, 0.18),
                        edgecolor=(0.3, 0.3, 0.3, 0.5), linewidth=0.5,
                        shade=False)


# Camera presets: (label, elev, azim). Azim matches character_views conventions
# (0=side, -90=front, 90=back, -45=3/4 front). Top is elev=85 to avoid gimbal.
CAMERA_PRESETS = [
    ("Front", 10,  -90),
    ("3Q",    15,  -65),
    ("Side",   5,    0),
    ("Back",  10,   90),
    ("Top",   85,  -90),
]


def remap_xyz(corners: np.ndarray) -> np.ndarray:
    """Model uses Y-up; matplotlib 3D uses Z-up. Swap Y↔Z for display."""
    out = corners.copy()
    out[:, [1, 2]] = out[:, [2, 1]]
    return out


# ── Interactive + snapshot entry points ────────────────────────────────────

def run_interactive(model_paths: list[Path], start_idx: int,
                    initial_clip: str,
                    hand_item: dict | None = None,
                    dummy: bool = False) -> None:
    """Interactive viewer with model+clip browsers, camera presets, playback.

    Controls (also keyboard-accessible):
      model   [<][>]          or  {  }      — switch between all discovered models
      clip    [<][>]          or  [  ]      — switch between (idle) + named clips
      camera  Front/3Q/…      or  1..5      — preset views
      [ ] walk cycle                        — apply base-part swings (run loop)
      [▶] play                or  space     — auto-advance the time slider
      speed  slider (0.1x-3x)               — playback rate
      t      slider           or  drag      — scrub manually (pauses auto-play)
    """
    # ── Load initial model ───────────────────────────────────────────────
    state = {
        "midx":       start_idx,
        "model":      load_model(model_paths[start_idx]),
        "clip":       initial_clip,
        "t":          0.0,
        "playing":    True,
        "speed":      1.0,
        "walk":       False,
        "hand_item":  hand_item,
        "dummy":      dummy,
    }
    state["clips"] = build_clips(state["model"])
    state["clip_names"] = build_clip_names(state["model"], state["clips"])
    if state["clip"] not in state["clip_names"]:
        state["clip"] = ""
    state["cidx"] = (state["clip_names"].index(state["clip"])
                     if state["clip"] else 0)

    # ── Figure layout ────────────────────────────────────────────────────
    fig = plt.figure(figsize=(12, 7.5))
    ax = fig.add_axes([0.04, 0.18, 0.60, 0.78], projection="3d")
    ax.view_init(elev=CAMERA_PRESETS[1][1], azim=CAMERA_PRESETS[1][2])

    # Right-side control column
    lbl_model = fig.text(0.68, 0.92, "", fontsize=10, family="monospace")
    lbl_clip  = fig.text(0.68, 0.82, "", fontsize=10, family="monospace")

    # ── Helpers ──────────────────────────────────────────────────────────
    def reload_model():
        state["model"] = load_model(model_paths[state["midx"]])
        state["clips"] = build_clips(state["model"])
        state["clip_names"] = build_clip_names(state["model"], state["clips"])
        state["cidx"] = min(state["cidx"], len(state["clip_names"]) - 1)
        state["clip"] = ("" if state["clip_names"][state["cidx"]] == "(idle)"
                         else state["clip_names"][state["cidx"]])
        state["t"] = 0.0

    def redraw():
        m = state["model"]
        name = state["clip"]
        # Three buckets: sinusoidal model clip, combo keyframe clip, or idle.
        sin_clip = state["clips"].get(name) if name and name in state["clips"] \
            else None
        combo = ""
        if name and name.startswith("combo:"):
            cname = name.split(":", 1)[1]
            if cname in ATTACK_CLIPS:
                combo = cname
        pivot_r = m.get("pivot_r") if combo else None
        corners = transform_all(m["parts"], state["t"], sin_clip,
                                walk_cycle=state["walk"],
                                combo_clip=combo, combo_pivot=pivot_r)
        corners = [remap_xyz(cr) for cr in corners]

        ep: list[dict] = []
        ec: list[np.ndarray] = []
        if state.get("hand_item") is not None and m.get("hand_r") is not None:
            hp, hc = held_item_corners(state["hand_item"], m["hand_r"],
                                       combo, pivot_r, state["t"])
            ep += hp
            ec += [remap_xyz(c) for c in hc]
        if state.get("dummy"):
            dp, dc = make_target_dummy()
            ep += dp
            ec += [remap_xyz(c) for c in dc]

        draw_model(ax, m["parts"], corners, show_ground=True,
                   extra_parts=ep or None, extra_corners=ec or None)

        lbl_model.set_text(
            f"model  {m.get('id', '?'):<16}  "
            f"({state['midx'] + 1}/{len(model_paths)})")
        lbl_clip.set_text(
            f"clip   {state['clip'] or '(idle)':<16}  "
            f"({state['cidx'] + 1}/{len(state['clip_names'])})   "
            f"t={state['t']:.2f}s")
        fig.canvas.draw_idle()

    # ── Model Prev/Next ──────────────────────────────────────────────────
    ax_mp = fig.add_axes([0.68, 0.86, 0.05, 0.05])
    ax_mn = fig.add_axes([0.74, 0.86, 0.05, 0.05])
    btn_mp = Button(ax_mp, "◀")
    btn_mn = Button(ax_mn, "▶")

    def step_model(delta):
        state["midx"] = (state["midx"] + delta) % len(model_paths)
        reload_model()
        redraw()
    btn_mp.on_clicked(lambda _: step_model(-1))
    btn_mn.on_clicked(lambda _: step_model(+1))

    # ── Clip Prev/Next ───────────────────────────────────────────────────
    ax_cp = fig.add_axes([0.68, 0.76, 0.05, 0.05])
    ax_cn = fig.add_axes([0.74, 0.76, 0.05, 0.05])
    btn_cp = Button(ax_cp, "◀")
    btn_cn = Button(ax_cn, "▶")

    def step_clip(delta):
        state["cidx"] = (state["cidx"] + delta) % len(state["clip_names"])
        state["clip"] = ("" if state["clip_names"][state["cidx"]] == "(idle)"
                         else state["clip_names"][state["cidx"]])
        state["t"] = 0.0
        redraw()
    btn_cp.on_clicked(lambda _: step_clip(-1))
    btn_cn.on_clicked(lambda _: step_clip(+1))

    # ── Camera presets (row of buttons) ──────────────────────────────────
    cam_buttons = []
    for i, (label, elev, azim) in enumerate(CAMERA_PRESETS):
        ax_b = fig.add_axes([0.68 + i * 0.06, 0.66, 0.055, 0.05])
        b = Button(ax_b, label)
        b.on_clicked(
            lambda _evt, e=elev, a=azim: (ax.view_init(elev=e, azim=a),
                                          fig.canvas.draw_idle()))
        cam_buttons.append(b)

    # ── Walk-cycle checkbox ──────────────────────────────────────────────
    ax_w = fig.add_axes([0.68, 0.56, 0.12, 0.05])
    chk_walk = CheckButtons(ax_w, ["walk cycle"], [False])

    def toggle_walk(_label):
        state["walk"] = not state["walk"]
        redraw()
    chk_walk.on_clicked(toggle_walk)

    # ── Play/Pause button ────────────────────────────────────────────────
    ax_pp = fig.add_axes([0.82, 0.56, 0.1, 0.05])
    btn_pp = Button(ax_pp, "❚❚ Pause")

    def toggle_play(_evt=None):
        state["playing"] = not state["playing"]
        btn_pp.label.set_text("❚❚ Pause" if state["playing"] else "▶ Play")
        fig.canvas.draw_idle()
    btn_pp.on_clicked(toggle_play)

    # ── Speed slider ─────────────────────────────────────────────────────
    ax_sp = fig.add_axes([0.68, 0.47, 0.25, 0.03])
    slider_sp = Slider(ax_sp, "speed", 0.1, 3.0, valinit=1.0)
    slider_sp.on_changed(lambda v: state.update(speed=float(v)))

    # ── Time slider ──────────────────────────────────────────────────────
    ax_t = fig.add_axes([0.08, 0.08, 0.56, 0.03])
    slider = Slider(ax_t, "t", 0.0, 4.0, valinit=0.0)

    def on_t(v):
        state["t"] = float(v)
        state["playing"] = False
        btn_pp.label.set_text("▶ Play")
        redraw()
    slider.on_changed(on_t)

    # ── Auto-advance timer ───────────────────────────────────────────────
    timer = fig.canvas.new_timer(interval=30)

    def tick():
        if state["playing"]:
            state["t"] = (state["t"] + 0.03 * state["speed"]) % 4.0
            slider.eventson = False
            slider.set_val(state["t"])
            slider.eventson = True
            redraw()
    timer.add_callback(tick)
    timer.start()

    # ── Keyboard shortcuts ───────────────────────────────────────────────
    def on_key(evt):
        k = evt.key
        if k == " ":
            toggle_play()
        elif k == "]":
            step_clip(+1)
        elif k == "[":
            step_clip(-1)
        elif k == "}":
            step_model(+1)
        elif k == "{":
            step_model(-1)
        elif k in "12345":
            i = int(k) - 1
            if i < len(CAMERA_PRESETS):
                _, e, a = CAMERA_PRESETS[i]
                ax.view_init(elev=e, azim=a)
                fig.canvas.draw_idle()
        elif k == "w":
            chk_walk.set_active(0)  # fires toggle_walk
    fig.canvas.mpl_connect("key_press_event", on_key)

    # ── Help text ────────────────────────────────────────────────────────
    fig.text(0.68, 0.18,
             "keys:\n"
             "  space  play/pause\n"
             "  [ ]    prev/next clip\n"
             "  { }    prev/next model\n"
             "  1-5    camera preset\n"
             "  w      walk cycle",
             fontsize=9, family="monospace", va="top")

    redraw()
    plt.show()


def run_snapshot(model: dict, clip_name: str, t: float, out_path: Path,
                 size_px: int, view: tuple[float, float],
                 held_item: dict | None = None,
                 dummy: bool = False) -> None:
    parts = model["parts"]
    clips = build_clips(model)
    combo = ""
    sin_clip = None
    if clip_name:
        if clip_name.startswith("combo:"):
            combo = clip_name.split(":", 1)[1]
        elif clip_name in ATTACK_CLIPS:
            combo = clip_name
        else:
            sin_clip = clips.get(clip_name)
    pivot_r = model.get("pivot_r") if combo else None
    corners = [remap_xyz(cr) for cr in transform_all(
        parts, t, sin_clip, combo_clip=combo, combo_pivot=pivot_r)]

    extra_parts: list[dict] = []
    extra_corners: list[np.ndarray] = []
    if held_item is not None and model.get("hand_r") is not None:
        hp, hc = held_item_corners(held_item, model["hand_r"],
                                   combo, pivot_r, t)
        extra_parts   += hp
        extra_corners += [remap_xyz(c) for c in hc]
    if dummy:
        dp, dc = make_target_dummy()
        extra_parts   += dp
        extra_corners += [remap_xyz(c) for c in dc]

    dpi = 100
    fig = plt.figure(figsize=(size_px / dpi, size_px / dpi), dpi=dpi)
    ax = fig.add_subplot(111, projection="3d")
    ax.view_init(elev=view[0], azim=view[1])
    draw_model(ax, parts, corners,
               extra_parts=extra_parts or None,
               extra_corners=extra_corners or None)
    ax.set_title(f"{model.get('id','?')} — {clip_name or 'idle'} @ t={t:.2f}",
                 y=1.0, pad=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=dpi)
    plt.close(fig)
    print(f"wrote {out_path}")


# ── Main ──────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("model_path", type=Path, nargs="?", default=None,
                    help="path to model .py file (omit to browse all discovered models)")
    ap.add_argument("--clip", default="",
                    help="named clip to play (default: idle)")
    ap.add_argument("--snapshot", type=Path, default=None,
                    help="write one PNG and exit instead of interactive")
    ap.add_argument("--time", type=float, default=0.25,
                    help="time (seconds) for --snapshot (default 0.25)")
    ap.add_argument("--size", type=int, default=600,
                    help="snapshot pixel size (square)")
    ap.add_argument("--elev", type=float, default=15.0,
                    help="snapshot camera elevation (degrees)")
    ap.add_argument("--azim", type=float, default=-65.0,
                    help="snapshot camera azimuth (degrees)")
    ap.add_argument("--hand", type=Path, default=None,
                    help="path to an item model .py to render in the right hand")
    ap.add_argument("--dummy", action="store_true",
                    help="render a training dummy in front of the character "
                         "(useful for verifying sword reach on combo clips)")
    args = ap.parse_args()

    hand_item = load_model(args.hand) if args.hand is not None else None

    # Snapshot mode: requires an explicit model path (one-shot render).
    if args.snapshot:
        if args.model_path is None:
            print("--snapshot requires a model_path", file=sys.stderr)
            sys.exit(2)
        model = load_model(args.model_path)
        if args.clip:
            available = set((model.get("clips") or {}).keys())
            combo_ok = (args.clip in ATTACK_CLIPS or
                        (args.clip.startswith("combo:") and
                         args.clip.split(":", 1)[1] in ATTACK_CLIPS))
            if args.clip not in available and not combo_ok:
                combos = sorted(ATTACK_CLIPS.keys())
                print(f"unknown clip {args.clip!r}. available: "
                      f"{sorted(available) or 'none'} "
                      f"| combo: {combos}", file=sys.stderr)
                sys.exit(2)
        run_snapshot(model, args.clip, args.time, args.snapshot,
                     args.size, (args.elev, args.azim),
                     held_item=hand_item, dummy=args.dummy)
        return

    # Interactive mode: browse every discovered model; start on the given
    # path (if any) or the first model in the list.
    repo_root = Path(__file__).resolve().parent.parent
    models = discover_models(repo_root)
    if not models:
        print(f"no models found under {repo_root}/src/*/artifacts/models/",
              file=sys.stderr)
        sys.exit(2)

    start_idx = 0
    if args.model_path is not None:
        target = args.model_path.resolve()
        for i, p in enumerate(models):
            if p.resolve() == target:
                start_idx = i
                break
        else:
            # Not in the discovered list — prepend it so the user still sees it.
            models.insert(0, args.model_path)
            start_idx = 0

    run_interactive(models, start_idx, args.clip, hand_item=hand_item)


if __name__ == "__main__":
    main()

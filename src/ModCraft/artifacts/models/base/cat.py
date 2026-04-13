"""Cat — four coat variants (orange tabby / black tuxedo / grey tabby / calico).

IMPORTANT: model_loader.h is a literal-only tokenizer — no Python variable
resolution. All colors/offsets must be inline literals. Parts tagged with
`"role": "..."` have their color swapped per variant, and `"hide"` in a
variant drops parts with those roles.

Roles used here:
    fur      — base coat
    stripe   — tabby markings (hidden on tuxedo, recolored on calico)
    belly    — underside / muzzle / socks / tail tip
    nose     — pink nose & pads
    ear_in   — inner ear
    eye      — iris
    whisker  — whisker dots / eye whites (kept neutral)

The loader emits `cat#0` .. `cat#3`; render picks by hash(entity_id) % 4.
"""

import math

model = {
    "id": "cat",
    "height": 0.5,
    "scale": 1.0,
    "walk_speed": 6.0,
    "idle_bob": 0.004,
    "walk_bob": 0.018,
    "head_pivot": [0, 0.34, -0.18],
    "parts": [
        # ═══ BODY ═══
        {"name": "torso", "role": "fur",
         "offset": [0, 0.25, 0], "size": [0.24, 0.20, 0.56], "color": [0.82, 0.48, 0.15, 1]},
        {"role": "belly",
         "offset": [0, 0.164, 0], "size": [0.22, 0.04, 0.50], "color": [0.92, 0.85, 0.72, 1]},
        # Tabby back stripes
        {"role": "stripe",
         "offset": [0, 0.361, -0.15], "size": [0.22, 0.01, 0.05], "color": [0.55, 0.28, 0.10, 1]},
        {"role": "stripe",
         "offset": [0, 0.361,  0.00], "size": [0.22, 0.01, 0.05], "color": [0.55, 0.28, 0.10, 1]},
        {"role": "stripe",
         "offset": [0, 0.361,  0.18], "size": [0.22, 0.01, 0.05], "color": [0.55, 0.28, 0.10, 1]},
        # Flank stripes
        {"role": "stripe",
         "offset": [-0.121, 0.24, -0.10], "size": [0.01, 0.12, 0.04], "color": [0.55, 0.28, 0.10, 1]},
        {"role": "stripe",
         "offset": [-0.121, 0.24,  0.12], "size": [0.01, 0.12, 0.04], "color": [0.55, 0.28, 0.10, 1]},
        {"role": "stripe",
         "offset": [ 0.121, 0.24, -0.10], "size": [0.01, 0.12, 0.04], "color": [0.55, 0.28, 0.10, 1]},
        {"role": "stripe",
         "offset": [ 0.121, 0.24,  0.12], "size": [0.01, 0.12, 0.04], "color": [0.55, 0.28, 0.10, 1]},

        # ═══ HEAD ═══
        {"name": "head", "head": True, "role": "fur",
         "offset": [0, 0.36, -0.30], "size": [0.228, 0.22, 0.22], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Forehead "M"
        {"head": True, "role": "stripe",
         "offset": [0, 0.471, -0.31], "size": [0.18, 0.01, 0.10], "color": [0.55, 0.28, 0.10, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Cream muzzle (pushed forward so front face doesn't z-fight with head front)
        {"head": True, "role": "belly",
         "offset": [0, 0.29, -0.396], "size": [0.14, 0.06, 0.04], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Pink nose (pushed forward past muzzle front face at z=-0.416)
        {"head": True, "role": "nose",
         "offset": [0, 0.33, -0.422], "size": [0.04, 0.03, 0.01], "color": [0.85, 0.45, 0.50, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},

        # Eye whites
        {"head": True, "role": "whisker",
         "offset": [-0.07, 0.40, -0.412], "size": [0.06, 0.05, 0.01], "color": [0.96, 0.93, 0.85, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True, "role": "whisker",
         "offset": [ 0.07, 0.40, -0.412], "size": [0.06, 0.05, 0.01], "color": [0.96, 0.93, 0.85, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Iris
        {"head": True, "role": "eye",
         "offset": [-0.07, 0.40, -0.418], "size": [0.04, 0.04, 0.01], "color": [0.70, 0.82, 0.25, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True, "role": "eye",
         "offset": [ 0.07, 0.40, -0.418], "size": [0.04, 0.04, 0.01], "color": [0.70, 0.82, 0.25, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        # Slit pupils
        {"head": True,
         "offset": [-0.07, 0.40, -0.423], "size": [0.01, 0.035, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True,
         "offset": [ 0.07, 0.40, -0.423], "size": [0.01, 0.035, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},

        # Whisker dots
        {"head": True,
         "offset": [-0.10, 0.31, -0.411], "size": [0.02, 0.01, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True,
         "offset": [-0.10, 0.29, -0.411], "size": [0.02, 0.01, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True,
         "offset": [ 0.10, 0.31, -0.411], "size": [0.02, 0.01, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True,
         "offset": [ 0.10, 0.29, -0.411], "size": [0.02, 0.01, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},

        # Ears
        {"head": True, "role": "fur",
         "offset": [-0.08, 0.48, -0.28], "size": [0.06, 0.12, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True, "role": "ear_in",
         "offset": [-0.08, 0.46, -0.27], "size": [0.03, 0.06, 0.03], "color": [0.70, 0.35, 0.35, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True, "role": "fur",
         "offset": [ 0.08, 0.48, -0.28], "size": [0.06, 0.12, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},
        {"head": True, "role": "ear_in",
         "offset": [ 0.08, 0.46, -0.27], "size": [0.03, 0.06, 0.03], "color": [0.70, 0.35, 0.35, 1],
         "pivot": [0, 0.34, -0.18], "swing_axis": [1, 0, 0], "amplitude": 8, "phase": 0, "speed": 0.5},

        # ═══ LEGS (fur shaft + belly-color sock tip) ═══
        {"role": "fur",
         "offset": [-0.06, 0.10, -0.16], "size": [0.06, 0.16, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [-0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "belly",
         "offset": [-0.06, 0.02, -0.16], "size": [0.07, 0.04, 0.07], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [-0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},

        {"role": "fur",
         "offset": [ 0.06, 0.10, -0.16], "size": [0.06, 0.16, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [ 0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        {"role": "belly",
         "offset": [ 0.06, 0.02, -0.16], "size": [0.07, 0.04, 0.07], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [ 0.06, 0.18, -0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},

        {"role": "fur",
         "offset": [-0.06, 0.10, 0.16], "size": [0.06, 0.16, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [-0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},
        {"role": "belly",
         "offset": [-0.06, 0.02, 0.16], "size": [0.07, 0.04, 0.07], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [-0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": math.pi, "speed": 1},

        {"role": "fur",
         "offset": [ 0.06, 0.10, 0.16], "size": [0.06, 0.16, 0.06], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [ 0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "belly",
         "offset": [ 0.06, 0.02, 0.16], "size": [0.07, 0.04, 0.07], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [ 0.06, 0.18, 0.16], "swing_axis": [1, 0, 0], "amplitude": 35, "phase": 0, "speed": 1},

        # ═══ TAIL (base + mid + striped tip) ═══
        {"role": "fur",
         "offset": [0, 0.30, 0.32], "size": [0.05, 0.05, 0.14], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 12, "phase": 0, "speed": 1.5},
        {"role": "stripe",
         "offset": [0, 0.30, 0.36], "size": [0.055, 0.055, 0.03], "color": [0.55, 0.28, 0.10, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 15, "phase": 0, "speed": 1.5},
        {"role": "fur",
         "offset": [0, 0.34, 0.42], "size": [0.045, 0.045, 0.10], "color": [0.82, 0.48, 0.15, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 18, "phase": 0.3, "speed": 1.5},
        {"role": "belly",
         "offset": [0, 0.38, 0.49], "size": [0.04, 0.04, 0.08], "color": [0.92, 0.85, 0.72, 1],
         "pivot": [0, 0.28, 0.28], "swing_axis": [1, 0, 0], "amplitude": 22, "phase": 0.5, "speed": 1.5},
    ]
}

# Per-instance variants. Render picks cat#(id % 4).
# `hide` drops parts with those roles; colors swap matching roles.
variants = [
    # 0 — Orange tabby (classic)
    {
        "fur":     [0.82, 0.48, 0.15, 1],
        "stripe":  [0.55, 0.28, 0.10, 1],
        "belly":   [0.92, 0.85, 0.72, 1],
        "nose":    [0.85, 0.45, 0.50, 1],
        "ear_in":  [0.70, 0.35, 0.35, 1],
        "eye":     [0.70, 0.82, 0.25, 1],
    },
    # 1 — Black tuxedo (no tabby stripes)
    {
        "fur":     [0.12, 0.12, 0.14, 1],
        "belly":   [0.95, 0.95, 0.92, 1],
        "nose":    [0.45, 0.25, 0.30, 1],
        "ear_in":  [0.55, 0.30, 0.35, 1],
        "eye":     [0.85, 0.70, 0.20, 1],
        "hide":    ["stripe"],
    },
    # 2 — Grey tabby (silver)
    {
        "fur":     [0.48, 0.48, 0.52, 1],
        "stripe":  [0.22, 0.22, 0.26, 1],
        "belly":   [0.80, 0.80, 0.78, 1],
        "nose":    [0.75, 0.50, 0.55, 1],
        "ear_in":  [0.60, 0.40, 0.45, 1],
        "eye":     [0.30, 0.70, 0.80, 1],
    },
    # 3 — Ginger-cream (light ginger, muted stripe)
    {
        "fur":     [0.85, 0.62, 0.30, 1],
        "stripe":  [0.55, 0.35, 0.15, 1],
        "belly":   [0.96, 0.93, 0.85, 1],
        "nose":    [0.85, 0.50, 0.55, 1],
        "ear_in":  [0.75, 0.45, 0.45, 1],
        "eye":     [0.55, 0.78, 0.40, 1],
    },
]

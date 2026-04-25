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
    "head_pivot": [0.0, 0.68, -0.36],
    "walk_bob": 0.036,
    "idle_bob": 0.008,
    "walk_speed": 6.0,
    "parts": [
        {"name": "torso", "role": "fur", "offset": [0.0, 0.5, 0.0], "size": [0.48, 0.4, 1.12], "color": [0.82, 0.48, 0.15, 1.0]},
        {"role": "belly", "offset": [0.0, 0.328, 0.0], "size": [0.44, 0.08, 1.0], "color": [0.92, 0.85, 0.72, 1.0]},
        {"role": "stripe", "offset": [0.0, 0.722, -0.3], "size": [0.44, 0.02, 0.1], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [0.0, 0.722, 0.0], "size": [0.44, 0.02, 0.1], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [0.0, 0.722, 0.36], "size": [0.44, 0.02, 0.1], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [-0.242, 0.48, -0.2], "size": [0.02, 0.24, 0.08], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [-0.242, 0.48, 0.24], "size": [0.02, 0.24, 0.08], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [0.242, 0.48, -0.2], "size": [0.02, 0.24, 0.08], "color": [0.55, 0.28, 0.1, 1.0]},
        {"role": "stripe", "offset": [0.242, 0.48, 0.24], "size": [0.02, 0.24, 0.08], "color": [0.55, 0.28, 0.1, 1.0]},
        {"name": "head", "role": "fur", "offset": [0.0, 0.72, -0.6], "size": [0.456, 0.44, 0.44], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "stripe", "offset": [0.0, 0.942, -0.62], "size": [0.36, 0.02, 0.2], "color": [0.55, 0.28, 0.1, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "belly", "offset": [0.0, 0.58, -0.792], "size": [0.28, 0.12, 0.08], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "nose", "offset": [0.0, 0.66, -0.844], "size": [0.08, 0.06, 0.02], "color": [0.85, 0.45, 0.5, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "whisker", "offset": [-0.14, 0.8, -0.824], "size": [0.12, 0.1, 0.02], "color": [0.96, 0.93, 0.85, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "whisker", "offset": [0.14, 0.8, -0.824], "size": [0.12, 0.1, 0.02], "color": [0.96, 0.93, 0.85, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "eye", "offset": [-0.14, 0.8, -0.836], "size": [0.08, 0.08, 0.02], "color": [0.7, 0.82, 0.25, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "eye", "offset": [0.14, 0.8, -0.836], "size": [0.08, 0.08, 0.02], "color": [0.7, 0.82, 0.25, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [-0.14, 0.8, -0.846], "size": [0.02, 0.07, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.14, 0.8, -0.846], "size": [0.02, 0.07, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [-0.2, 0.62, -0.822], "size": [0.04, 0.02, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [-0.2, 0.58, -0.822], "size": [0.04, 0.02, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.2, 0.62, -0.822], "size": [0.04, 0.02, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.2, 0.58, -0.822], "size": [0.04, 0.02, 0.02], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "fur", "offset": [-0.16, 0.96, -0.56], "size": [0.12, 0.24, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "ear_in", "offset": [-0.16, 0.92, -0.54], "size": [0.06, 0.12, 0.06], "color": [0.7, 0.35, 0.35, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "fur", "offset": [0.16, 0.96, -0.56], "size": [0.12, 0.24, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "ear_in", "offset": [0.16, 0.92, -0.54], "size": [0.06, 0.12, 0.06], "color": [0.7, 0.35, 0.35, 1.0], "pivot": [0.0, 0.68, -0.36], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.5, "head": True},
        {"role": "fur", "offset": [-0.12, 0.2, -0.32], "size": [0.12, 0.32, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [-0.12, 0.36, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "belly", "offset": [-0.12, 0.04, -0.32], "size": [0.14, 0.08, 0.14], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [-0.12, 0.36, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "fur", "offset": [0.12, 0.2, -0.32], "size": [0.12, 0.32, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.12, 0.36, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"role": "belly", "offset": [0.12, 0.04, -0.32], "size": [0.14, 0.08, 0.14], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [0.12, 0.36, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"role": "fur", "offset": [-0.12, 0.2, 0.32], "size": [0.12, 0.32, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [-0.12, 0.36, 0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"role": "belly", "offset": [-0.12, 0.04, 0.32], "size": [0.14, 0.08, 0.14], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [-0.12, 0.36, 0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"role": "fur", "offset": [0.12, 0.2, 0.32], "size": [0.12, 0.32, 0.12], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.12, 0.36, 0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "belly", "offset": [0.12, 0.04, 0.32], "size": [0.14, 0.08, 0.14], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [0.12, 0.36, 0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"role": "fur", "offset": [0.0, 0.6, 0.64], "size": [0.1, 0.1, 0.28], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.0, 0.56, 0.56], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 12, "phase": 0, "speed": 1.5},
        {"role": "stripe", "offset": [0.0, 0.6, 0.72], "size": [0.11, 0.11, 0.06], "color": [0.55, 0.28, 0.1, 1.0], "pivot": [0.0, 0.56, 0.56], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5},
        {"role": "fur", "offset": [0.0, 0.68, 0.84], "size": [0.09, 0.09, 0.2], "color": [0.82, 0.48, 0.15, 1.0], "pivot": [0.0, 0.56, 0.56], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 18, "phase": 0.3, "speed": 1.5},
        {"role": "belly", "offset": [0.0, 0.76, 0.98], "size": [0.08, 0.08, 0.16], "color": [0.92, 0.85, 0.72, 1.0], "pivot": [0.0, 0.56, 0.56], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 22, "phase": 0.5, "speed": 1.5},
    ],
}

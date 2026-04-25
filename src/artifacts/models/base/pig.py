"""Pig — fun, characterful farm animal with bold saturated pink + spots.

NOTE on the palette: the C++ model_loader is a minimal tokenizer — it does
NOT resolve Python variables like BASE = [0.85, ...] used by reference.
All colors MUST be inline [r,g,b,a] arrays or they come out as default white.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase, speed

Palette (for human reference only — kept in sync with inline literals below):
    BASE    [0.88, 0.70, 0.62, 1]   # true saturated pink
    BELLY   [0.95, 0.72, 0.72, 1]   # lighter pink underside
    SNOUT   [0.72, 0.48, 0.42, 1]   # deep rose
    EAR_IN  [0.58, 0.38, 0.35, 1]   # dark rose
    HOOF    [0.18, 0.11, 0.08, 1]   # near-black brown
    SPOT    [0.35, 0.20, 0.22, 1]   # dairy-pig dark spots
    MUD     [0.38, 0.24, 0.14, 1]   # mud
    EYE     [0.04, 0.03, 0.03, 1]   # pupil
    EYE_WH  [0.95, 0.93, 0.88, 1]   # eye-white
    BANDANA [0.92, 0.68, 0.18, 1]   # golden-yellow scarf
    TAIL_T  [0.62, 0.25, 0.30, 1]   # tail tip
"""

import math

model = {
    "id": "pig",
    "head_pivot": [0.0, 0.6875, -0.3125],
    "walk_speed": 7.0,
    "parts": [
        {"name": "torso", "role": "body", "offset": [0.0, 0.5625, 0.0], "size": [0.75, 0.625, 1.0], "color": [0.88, 0.7, 0.62, 1.0]},
        {"role": "belly", "offset": [0.0, 0.2875, 0.0], "size": [0.725, 0.1, 0.95], "color": [0.95, 0.82, 0.76, 1.0]},
        {"role": "bandana", "offset": [0.0, 0.7, -0.275], "size": [0.775, 0.1, 0.275], "color": [0.92, 0.68, 0.18, 1.0]},
        {"role": "bandana", "offset": [-0.4, 0.7, -0.275], "size": [0.1, 0.075, 0.125], "color": [0.92, 0.68, 0.18, 1.0]},
        {"name": "head", "role": "body", "offset": [0.0, 0.6875, -0.5625], "size": [0.55, 0.5, 0.5], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"offset": [-0.1375, 0.75, -0.8188], "size": [0.0375, 0.0375, 0.0125], "color": [0.12, 0.08, 0.08, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"offset": [0.1375, 0.75, -0.8188], "size": [0.0375, 0.0375, 0.0125], "color": [0.12, 0.08, 0.08, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "snout", "offset": [0.0, 0.6, -0.8], "size": [0.3, 0.2, 0.125], "color": [0.72, 0.48, 0.42, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"offset": [-0.075, 0.6, -0.8637], "size": [0.05, 0.0625, 0.0125], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"offset": [0.075, 0.6, -0.8637], "size": [0.05, 0.0625, 0.0125], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "body", "offset": [-0.225, 0.9, -0.525], "size": [0.15, 0.125, 0.2], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "ear_in", "offset": [-0.225, 0.8812, -0.5125], "size": [0.075, 0.075, 0.125], "color": [0.58, 0.38, 0.35, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "body", "offset": [0.225, 0.9, -0.525], "size": [0.15, 0.125, 0.2], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "ear_in", "offset": [0.225, 0.8812, -0.5125], "size": [0.075, 0.075, 0.125], "color": [0.58, 0.38, 0.35, 1.0], "pivot": [0.0, 0.6875, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "speed": 0.5, "head": True},
        {"role": "body", "offset": [-0.225, 0.225, -0.3125], "size": [0.175, 0.3, 0.175], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [-0.225, 0.375, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0},
        {"role": "hoof", "offset": [-0.225, 0.05, -0.3125], "size": [0.1875, 0.075, 0.1875], "color": [0.18, 0.11, 0.08, 1.0], "pivot": [-0.225, 0.375, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0},
        {"role": "body", "offset": [0.225, 0.225, -0.3125], "size": [0.175, 0.3, 0.175], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.225, 0.375, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416},
        {"role": "hoof", "offset": [0.225, 0.05, -0.3125], "size": [0.1875, 0.075, 0.1875], "color": [0.18, 0.11, 0.08, 1.0], "pivot": [0.225, 0.375, -0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416},
        {"role": "body", "offset": [-0.225, 0.225, 0.3125], "size": [0.175, 0.3, 0.175], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [-0.225, 0.375, 0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416},
        {"role": "hoof", "offset": [-0.225, 0.05, 0.3125], "size": [0.1875, 0.075, 0.1875], "color": [0.18, 0.11, 0.08, 1.0], "pivot": [-0.225, 0.375, 0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 3.1416},
        {"role": "body", "offset": [0.225, 0.225, 0.3125], "size": [0.175, 0.3, 0.175], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.225, 0.375, 0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0},
        {"role": "hoof", "offset": [0.225, 0.05, 0.3125], "size": [0.1875, 0.075, 0.1875], "color": [0.18, 0.11, 0.08, 1.0], "pivot": [0.225, 0.375, 0.3125], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 30, "phase": 0},
        {"role": "body", "offset": [0.0, 0.6875, 0.5375], "size": [0.0875, 0.0875, 0.1], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.0, 0.625, 0.5], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 15, "speed": 2},
        {"role": "body", "offset": [0.0625, 0.775, 0.5875], "size": [0.075, 0.075, 0.075], "color": [0.88, 0.7, 0.62, 1.0], "pivot": [0.0, 0.625, 0.5], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 18, "speed": 2},
        {"role": "tail_tip", "offset": [0.0, 0.85, 0.55], "size": [0.0625, 0.0625, 0.0625], "color": [0.68, 0.42, 0.38, 1.0], "pivot": [0.0, 0.625, 0.5], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 22, "speed": 2},
    ],
}

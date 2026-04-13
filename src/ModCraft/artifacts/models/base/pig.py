"""Pig — fun, characterful farm animal with bold saturated pink + spots.

NOTE on the palette: the C++ model_loader is a minimal tokenizer — it does
NOT resolve Python variables like BASE = [0.85, ...] used by reference.
All colors MUST be inline [r,g,b,a] arrays or they come out as default white.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase, speed

Palette (for human reference only — kept in sync with inline literals below):
    BASE    [0.85, 0.45, 0.52, 1]   # true saturated pink
    BELLY   [0.95, 0.72, 0.72, 1]   # lighter pink underside
    SNOUT   [0.70, 0.28, 0.38, 1]   # deep rose
    EAR_IN  [0.55, 0.20, 0.30, 1]   # dark rose
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
    "height": 0.9,
    "scale": 1.25,
    "walk_speed": 7.0,
    "head_pivot": [0, 0.55, -0.25],
    "parts": [
        # ═══ BODY — saturated pink ═══
        {"name": "torso",
         "offset": [0, 0.45, 0], "size": [0.60, 0.50, 0.80], "color": [0.85, 0.45, 0.52, 1]},
        # Lighter belly patch
        {"offset": [0, 0.23, 0], "size": [0.58, 0.08, 0.76], "color": [0.95, 0.72, 0.72, 1]},

        # ═══ DAIRY-PIG DARK SPOTS on back + flanks ═══
        # Back spots: raised so top face pokes clearly above torso top (0.70).
        {"offset": [-0.15, 0.706, -0.05], "size": [0.14, 0.02, 0.18], "color": [0.35, 0.20, 0.22, 1]},
        {"offset": [ 0.18, 0.706,  0.12], "size": [0.10, 0.02, 0.14], "color": [0.35, 0.20, 0.22, 1]},
        # Flank spots: pushed outward (torso half-x=0.30) so outer face is ~0.008 past body.
        {"offset": [ 0.304, 0.45,  0.15], "size": [0.01, 0.18, 0.20], "color": [0.35, 0.20, 0.22, 1]},
        {"offset": [-0.304, 0.50, -0.12], "size": [0.01, 0.12, 0.16], "color": [0.35, 0.20, 0.22, 1]},
        # Mud splotches
        {"offset": [ 0.304, 0.28,  0.25], "size": [0.01, 0.08, 0.12], "color": [0.38, 0.24, 0.14, 1]},
        {"offset": [-0.304, 0.30,  0.22], "size": [0.01, 0.06, 0.10], "color": [0.38, 0.24, 0.14, 1]},

        # ═══ GOLDEN BANDANA ═══
        {"offset": [0, 0.56, -0.22], "size": [0.62, 0.08, 0.22], "color": [0.92, 0.68, 0.18, 1]},
        {"offset": [-0.32, 0.56, -0.22], "size": [0.08, 0.06, 0.10], "color": [0.92, 0.68, 0.18, 1]},

        # ═══ HEAD ═══
        {"name": "head", "head": True,
         "offset": [0, 0.55, -0.45], "size": [0.44, 0.40, 0.40], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},

        # Cheek spots (pushed out: head half-x=0.22, spot outer face at x=0.228)
        {"head": True,
         "offset": [-0.224, 0.50, -0.50], "size": [0.01, 0.08, 0.10], "color": [0.35, 0.20, 0.22, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.224, 0.50, -0.50], "size": [0.01, 0.08, 0.10], "color": [0.35, 0.20, 0.22, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},

        # Eye whites
        {"head": True,
         "offset": [-0.13, 0.60, -0.649], "size": [0.08, 0.08, 0.02], "color": [0.95, 0.93, 0.88, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.13, 0.60, -0.649], "size": [0.08, 0.08, 0.02], "color": [0.95, 0.93, 0.88, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        # Pupils
        {"head": True,
         "offset": [-0.13, 0.60, -0.660], "size": [0.04, 0.04, 0.02], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.13, 0.60, -0.660], "size": [0.04, 0.04, 0.02], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},

        # Snout (deep rose)
        {"head": True,
         "offset": [0, 0.48, -0.64], "size": [0.24, 0.16, 0.10], "color": [0.70, 0.28, 0.38, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        # Nostrils
        {"head": True,
         "offset": [-0.06, 0.48, -0.691], "size": [0.04, 0.05, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.06, 0.48, -0.691], "size": [0.04, 0.05, 0.01], "color": [0.04, 0.03, 0.03, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},

        # Ears — outer (pink) + inner (darker rose)
        {"head": True,
         "offset": [-0.18, 0.72, -0.42], "size": [0.12, 0.10, 0.16], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [-0.18, 0.705, -0.41], "size": [0.06, 0.06, 0.10], "color": [0.55, 0.20, 0.30, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.18, 0.72, -0.42], "size": [0.12, 0.10, 0.16], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},
        {"head": True,
         "offset": [ 0.18, 0.705, -0.41], "size": [0.06, 0.06, 0.10], "color": [0.55, 0.20, 0.30, 1],
         "pivot": [0, 0.55, -0.25], "swing_axis": [1,0,0], "amplitude": 8, "speed": 0.5},

        # ═══ LEGS (pink + dark hoof cap) ═══
        {"offset": [-0.18, 0.18, -0.25], "size": [0.14, 0.24, 0.14], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [-0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},
        {"offset": [-0.18, 0.04, -0.25], "size": [0.15, 0.06, 0.15], "color": [0.18, 0.11, 0.08, 1],
         "pivot": [-0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},

        {"offset": [ 0.18, 0.18, -0.25], "size": [0.14, 0.24, 0.14], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [ 0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},
        {"offset": [ 0.18, 0.04, -0.25], "size": [0.15, 0.06, 0.15], "color": [0.18, 0.11, 0.08, 1],
         "pivot": [ 0.18, 0.30, -0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},

        {"offset": [-0.18, 0.18, 0.25], "size": [0.14, 0.24, 0.14], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [-0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},
        {"offset": [-0.18, 0.04, 0.25], "size": [0.15, 0.06, 0.15], "color": [0.18, 0.11, 0.08, 1],
         "pivot": [-0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": math.pi},

        {"offset": [ 0.18, 0.18, 0.25], "size": [0.14, 0.24, 0.14], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [ 0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},
        {"offset": [ 0.18, 0.04, 0.25], "size": [0.15, 0.06, 0.15], "color": [0.18, 0.11, 0.08, 1],
         "pivot": [ 0.18, 0.30, 0.25], "swing_axis": [1,0,0], "amplitude": 30, "phase": 0},

        # ═══ CURLY TAIL (3-voxel spiral) ═══
        {"offset": [0,    0.55, 0.43], "size": [0.07, 0.07, 0.08], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [0, 0.50, 0.40], "swing_axis": [0,1,0], "amplitude": 15, "speed": 2},
        {"offset": [0.05, 0.62, 0.47], "size": [0.06, 0.06, 0.06], "color": [0.85, 0.45, 0.52, 1],
         "pivot": [0, 0.50, 0.40], "swing_axis": [0,1,0], "amplitude": 18, "speed": 2},
        {"offset": [0,    0.68, 0.44], "size": [0.05, 0.05, 0.05], "color": [0.62, 0.25, 0.30, 1],
         "pivot": [0, 0.50, 0.40], "swing_axis": [0,1,0], "amplitude": 22, "speed": 2},
    ]
}

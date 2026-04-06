"""Purple Mage -- arcane robes, tall hat, staff with gem.

Wielder of arcane arts, draped in star-dusted robes.
STR 1  STA 2  AGI 3  INT 5

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

import math

model = {
    "id": "mage",
    "height": 2.4,
    "scale": 1.0,
    "hand_r":  [ 0.52,  0.82, -0.12],
    "hand_l":  [-0.52,  0.82, -0.12],
    "pivot_r": [ 0.32,  1.40,  0.00],
    "pivot_l": [-0.32,  1.40,  0.00],
    "walk_speed": 1.7,
    "idle_bob": 0.010,
    "walk_bob": 0.040,
    "parts": [
        # Head (texture provides face -- must be parts[0])
        {"offset": [0, 1.65, 0], "size": [0.44, 0.44, 0.44], "color": [0.92, 0.82, 0.70, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},

        # Robe base (wide hem)
        {"offset": [0, 0.40, 0], "size": [0.68, 0.20, 0.48], "color": [0.36, 0.06, 0.54, 1]},
        # Robe body
        {"offset": [0, 0.64, 0], "size": [0.60, 0.48, 0.44], "color": [0.45, 0.10, 0.65, 1]},
        # Robe upper
        {"offset": [0, 1.08, 0], "size": [0.44, 0.44, 0.32], "color": [0.45, 0.10, 0.65, 1]},
        # Chest panel
        {"offset": [0, 1.08, -0.15], "size": [0.28, 0.32, 0.04], "color": [0.60, 0.20, 0.84, 1]},
        # Belt
        {"offset": [0, 0.87, -0.21], "size": [0.36, 0.06, 0.04], "color": [0.80, 0.68, 0.12, 1]},
        # Belt clasp
        {"offset": [0, 0.87, -0.23], "size": [0.08, 0.08, 0.02], "color": [0.95, 0.88, 0.20, 1]},

        # Hat brim
        {"offset": [0, 1.89, 0], "size": [0.56, 0.08, 0.56], "color": [0.22, 0.05, 0.34, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Hat gold band
        {"offset": [0, 1.94, 0], "size": [0.42, 0.05, 0.42], "color": [0.80, 0.68, 0.12, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Hat lower cone
        {"offset": [0, 2.02, 0], "size": [0.36, 0.16, 0.36], "color": [0.22, 0.05, 0.34, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Hat mid cone
        {"offset": [0, 2.16, 0], "size": [0.24, 0.20, 0.24], "color": [0.22, 0.05, 0.34, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Hat tip
        {"offset": [0, 2.32, 0], "size": [0.14, 0.28, 0.14], "color": [0.22, 0.05, 0.34, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},
        # Hat star emblem
        {"offset": [0.07, 2.10, -0.12], "size": [0.06, 0.06, 0.02], "color": [0.95, 0.88, 0.20, 1],
         "pivot": [0, 1.44, 0], "swing_axis": [1, 0, 0], "amplitude": 5, "phase": 0, "speed": 2},

        # Left arm
        {"offset": [-0.32, 1.08, 0], "size": [0.20, 0.60, 0.20], "color": [0.45, 0.10, 0.65, 1],
         "pivot": [-0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left bell sleeve
        {"offset": [-0.32, 0.82, 0], "size": [0.28, 0.12, 0.24], "color": [0.36, 0.06, 0.54, 1],
         "pivot": [-0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Left hand
        {"offset": [-0.32, 0.74, 0], "size": [0.16, 0.10, 0.14], "color": [0.92, 0.82, 0.70, 1],
         "pivot": [-0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": math.pi, "speed": 1},
        # Right arm
        {"offset": [0.32, 1.08, 0], "size": [0.20, 0.60, 0.20], "color": [0.45, 0.10, 0.65, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right bell sleeve
        {"offset": [0.32, 0.82, 0], "size": [0.28, 0.12, 0.24], "color": [0.36, 0.06, 0.54, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Right hand
        {"offset": [0.32, 0.74, 0], "size": [0.16, 0.10, 0.14], "color": [0.92, 0.82, 0.70, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # Staff shaft
        {"offset": [0.44, 0.82, -0.08], "size": [0.06, 0.96, 0.06], "color": [0.40, 0.28, 0.12, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Staff gem (outer glow)
        {"offset": [0.44, 1.34, -0.08], "size": [0.18, 0.18, 0.18], "color": [0.38, 0.70, 1.00, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},
        # Staff gem (inner core)
        {"offset": [0.44, 1.34, -0.08], "size": [0.10, 0.10, 0.10], "color": [0.76, 0.92, 1.00, 1],
         "pivot": [0.32, 1.40, 0], "swing_axis": [1, 0, 0], "amplitude": 50, "phase": 0, "speed": 1},

        # Left leg (peeks below robe)
        {"offset": [-0.10, 0.22, 0], "size": [0.18, 0.44, 0.20], "color": [0.32, 0.06, 0.48, 1],
         "pivot": [-0.10, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 45, "phase": 0, "speed": 1},
        # Right leg
        {"offset": [0.10, 0.22, 0], "size": [0.18, 0.44, 0.20], "color": [0.32, 0.06, 0.48, 1],
         "pivot": [0.10, 0.48, 0], "swing_axis": [1, 0, 0], "amplitude": 45, "phase": math.pi, "speed": 1},
    ]
}

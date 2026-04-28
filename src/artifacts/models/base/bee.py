"""Bee — tiny yellow-and-black striped flyer with translucent wings.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "bee",
    "head_pivot": [0.0, 0.48, -0.32],
    "walk_bob": 0.06,
    "idle_bob": 0.1,
    "walk_speed": 4.0,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.48, 0.08], "size": [0.72, 0.64, 0.88], "color": [0.95, 0.78, 0.15, 1.0]},
        {"offset": [0.0, 0.8, -0.16], "size": [0.76, 0.06, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [0.0, 0.8, 0.24], "size": [0.76, 0.06, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [-0.364, 0.48, -0.16], "size": [0.02, 0.68, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [0.364, 0.48, -0.16], "size": [0.02, 0.68, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [-0.364, 0.48, 0.24], "size": [0.02, 0.68, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [0.364, 0.48, 0.24], "size": [0.02, 0.68, 0.16], "color": [0.12, 0.1, 0.08, 1.0]},
        {"offset": [0.0, 0.48, 0.64], "size": [0.12, 0.12, 0.16], "color": [0.08, 0.06, 0.05, 1.0]},
        {"name": "head", "offset": [0.0, 0.48, -0.56], "size": [0.56, 0.56, 0.48], "color": [0.15, 0.12, 0.08, 1.0], "pivot": [0.0, 0.48, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [-0.264, 0.52, -0.68], "size": [0.06, 0.32, 0.28], "color": [0.1, 0.08, 0.08, 1.0], "pivot": [0.0, 0.48, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.264, 0.52, -0.68], "size": [0.06, 0.32, 0.28], "color": [0.1, 0.08, 0.08, 1.0], "pivot": [0.0, 0.48, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [-0.16, 0.88, -0.64], "size": [0.048, 0.32, 0.048], "color": [0.1, 0.08, 0.06, 1.0], "pivot": [0.0, 0.48, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.16, 0.88, -0.64], "size": [0.048, 0.32, 0.048], "color": [0.1, 0.08, 0.06, 1.0], "pivot": [0.0, 0.48, -0.32], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 6, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [-0.52, 0.88, 0.0], "size": [0.48, 0.08, 0.72], "color": [0.9, 0.92, 0.95, 1.0], "pivot": [-0.2, 0.88, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 22, "phase": 0, "speed": 12},
        {"offset": [0.52, 0.88, 0.0], "size": [0.48, 0.08, 0.72], "color": [0.9, 0.92, 0.95, 1.0], "pivot": [0.2, 0.88, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 22, "phase": 3.1416, "speed": 12},
        {"offset": [-0.28, 0.08, -0.16], "size": [0.08, 0.24, 0.08], "color": [0.1, 0.08, 0.06, 1.0]},
        {"offset": [0.28, 0.08, -0.16], "size": [0.08, 0.24, 0.08], "color": [0.1, 0.08, 0.06, 1.0]},
        {"offset": [-0.28, 0.08, 0.24], "size": [0.08, 0.24, 0.08], "color": [0.1, 0.08, 0.06, 1.0]},
        {"offset": [0.28, 0.08, 0.24], "size": [0.08, 0.24, 0.08], "color": [0.1, 0.08, 0.06, 1.0]},
    ],
}

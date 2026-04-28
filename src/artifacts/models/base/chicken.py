"""Chicken — small round bird with thin legs.

Fast leg cycles, wings flap slightly, head bobs.
Edit parts to customize the chicken's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "chicken",
    "head_pivot": [0.0, 0.6435, -0.143],
    "walk_bob": 0.0214,
    "idle_bob": 0.0072,
    "walk_speed": 9.0,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.4576, 0.0], "size": [0.4576, 0.4004, 0.6292], "color": [0.95, 0.95, 0.9, 1.0]},
        {"name": "head", "offset": [0.0, 0.7865, -0.3432], "size": [0.286, 0.286, 0.286], "color": [0.95, 0.95, 0.92, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.0, 0.7436, -0.5005], "size": [0.1144, 0.0858, 0.143], "color": [0.95, 0.7, 0.2, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.0, 0.9438, -0.3146], "size": [0.0858, 0.143, 0.1716], "color": [0.9, 0.15, 0.1, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.0, 0.6292, -0.4719], "size": [0.0858, 0.1144, 0.0572], "color": [0.9, 0.2, 0.15, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [-0.1444, 0.8294, -0.3718], "size": [0.0143, 0.0572, 0.0572], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [0.1444, 0.8294, -0.3718], "size": [0.0143, 0.0572, 0.0572], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.6435, -0.143], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 15, "phase": 0, "speed": 1.5, "head": True},
        {"offset": [-0.2574, 0.4719, 0.0286], "size": [0.1144, 0.286, 0.4576], "color": [0.92, 0.92, 0.87, 1.0], "pivot": [-0.2002, 0.6006, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 12, "phase": 0, "speed": 1},
        {"offset": [0.2574, 0.4719, 0.0286], "size": [0.1144, 0.286, 0.4576], "color": [0.92, 0.92, 0.87, 1.0], "pivot": [0.2002, 0.6006, 0.0], "swing_axis": [0.0, 0.0, 1.0], "amplitude": 12, "phase": 3.1416, "speed": 1},
        {"offset": [-0.1001, 0.1144, 0.0], "size": [0.0858, 0.286, 0.0858], "color": [0.9, 0.7, 0.2, 1.0], "pivot": [-0.1001, 0.2574, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"offset": [0.1001, 0.1144, 0.0], "size": [0.0858, 0.286, 0.0858], "color": [0.9, 0.7, 0.2, 1.0], "pivot": [0.1001, 0.2574, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"offset": [0.0, 0.6006, 0.3718], "size": [0.2288, 0.3432, 0.1144], "color": [0.88, 0.88, 0.82, 1.0]},
        {"offset": [0.0, 0.8008, 0.4147], "size": [0.1144, 0.2002, 0.0715], "color": [0.74, 0.74, 0.68, 1.0]},
        {"offset": [-0.1573, 0.572, 0.4004], "size": [0.0858, 0.2574, 0.0715], "color": [0.82, 0.82, 0.76, 1.0]},
        {"offset": [0.1573, 0.572, 0.4004], "size": [0.0858, 0.2574, 0.0715], "color": [0.82, 0.82, 0.76, 1.0]},
    ],
}

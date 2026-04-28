"""Squirrel — small quick rodent with a huge bushy tail.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "squirrel",
    "head_pivot": [0.0, 0.8658, -0.333],
    "walk_bob": 0.0666,
    "idle_bob": 0.02,
    "walk_speed": 10.0,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.5994, 0.0], "size": [0.5328, 0.5328, 0.9324], "color": [0.55, 0.32, 0.15, 1.0]},
        {"name": "head", "offset": [0.0, 0.9324, -0.666], "size": [0.5328, 0.4662, 0.4662], "color": [0.58, 0.34, 0.17, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.0, 0.7992, -0.9158], "size": [0.2664, 0.1665, 0.0999], "color": [0.92, 0.88, 0.78, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.0, 0.8658, -0.969], "size": [0.0666, 0.0666, 0.0333], "color": [0.12, 0.08, 0.06, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [-0.1998, 0.999, -0.9024], "size": [0.0666, 0.0833, 0.0333], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.1998, 0.999, -0.9024], "size": [0.0666, 0.0833, 0.0333], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [-0.1832, 1.2654, -0.5994], "size": [0.1332, 0.2331, 0.1332], "color": [0.5, 0.28, 0.12, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.1832, 1.2654, -0.5994], "size": [0.1332, 0.2331, 0.1332], "color": [0.5, 0.28, 0.12, 1.0], "pivot": [0.0, 0.8658, -0.333], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 1.0, "head": True},
        {"offset": [0.0, 0.383, -0.1332], "size": [0.333, 0.1332, 0.5328], "color": [0.92, 0.88, 0.78, 1.0]},
        {"offset": [-0.1998, 0.1998, -0.2997], "size": [0.1665, 0.3996, 0.1665], "color": [0.52, 0.3, 0.13, 1.0], "pivot": [-0.1998, 0.3996, -0.2997], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 38, "phase": 0, "speed": 1},
        {"offset": [0.1998, 0.1998, -0.2997], "size": [0.1665, 0.3996, 0.1665], "color": [0.52, 0.3, 0.13, 1.0], "pivot": [0.1998, 0.3996, -0.2997], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 38, "phase": 3.1416, "speed": 1},
        {"offset": [-0.2331, 0.1998, 0.2664], "size": [0.1998, 0.3996, 0.2664], "color": [0.52, 0.3, 0.13, 1.0], "pivot": [-0.2331, 0.3996, 0.2664], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 38, "phase": 3.1416, "speed": 1},
        {"offset": [0.2331, 0.1998, 0.2664], "size": [0.1998, 0.3996, 0.2664], "color": [0.52, 0.3, 0.13, 1.0], "pivot": [0.2331, 0.3996, 0.2664], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 38, "phase": 0, "speed": 1},
        {"offset": [0.0, 0.7992, 0.5994], "size": [0.333, 0.3996, 0.2664], "color": [0.62, 0.38, 0.18, 1.0], "pivot": [0.0, 0.7326, 0.5994], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.1988, 0.666], "size": [0.3996, 0.4662, 0.2664], "color": [0.65, 0.4, 0.2, 1.0], "pivot": [0.0, 0.7326, 0.5994], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 2},
        {"offset": [0.0, 1.5984, 0.5328], "size": [0.4662, 0.3996, 0.333], "color": [0.75, 0.55, 0.28, 1.0], "pivot": [0.0, 0.7326, 0.5994], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 2},
    ],
}

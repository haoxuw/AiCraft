"""Raccoon — chunky trash-bandit with a ringed tail and black eye mask.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "raccoon",
    "head_pivot": [0.0, 0.7644, -0.3276],
    "walk_bob": 0.0455,
    "idle_bob": 0.0127,
    "walk_speed": 5.5,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.5824, 0.0], "size": [0.5824, 0.5096, 1.0192], "color": [0.45, 0.45, 0.48, 1.0]},
        {"offset": [0.0, 0.839, 0.0], "size": [0.3276, 0.0182, 0.91], "color": [0.22, 0.22, 0.24, 1.0]},
        {"offset": [0.0, 0.3422, 0.0], "size": [0.5096, 0.0546, 0.91], "color": [0.6, 0.58, 0.55, 1.0]},
        {"name": "head", "offset": [0.0, 0.8008, -0.5824], "size": [0.5096, 0.4368, 0.4368], "color": [0.55, 0.55, 0.58, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.0, 0.6916, -0.8281], "size": [0.2548, 0.1456, 0.091], "color": [0.9, 0.88, 0.82, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.0, 0.728, -0.8845], "size": [0.0728, 0.0728, 0.0364], "color": [0.08, 0.06, 0.05, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.1456, 0.8554, -0.8026], "size": [0.2184, 0.1092, 0.0182], "color": [0.1, 0.08, 0.08, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.1456, 0.8554, -0.8026], "size": [0.2184, 0.1092, 0.0182], "color": [0.1, 0.08, 0.08, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.1456, 0.8554, -0.8154], "size": [0.0546, 0.0546, 0.0091], "color": [0.92, 0.85, 0.3, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.1456, 0.8554, -0.8154], "size": [0.0546, 0.0546, 0.0091], "color": [0.92, 0.85, 0.3, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.182, 1.0738, -0.5096], "size": [0.1092, 0.1456, 0.1092], "color": [0.38, 0.38, 0.42, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [0.182, 1.0738, -0.5096], "size": [0.1092, 0.1456, 0.1092], "color": [0.38, 0.38, 0.42, 1.0], "pivot": [0.0, 0.7644, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 8, "phase": 0, "speed": 0.8, "head": True},
        {"offset": [-0.182, 0.182, -0.3276], "size": [0.1456, 0.364, 0.1456], "color": [0.28, 0.26, 0.26, 1.0], "pivot": [-0.182, 0.4004, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"offset": [0.182, 0.182, -0.3276], "size": [0.1456, 0.364, 0.1456], "color": [0.28, 0.26, 0.26, 1.0], "pivot": [0.182, 0.4004, -0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"offset": [-0.182, 0.182, 0.3276], "size": [0.1456, 0.364, 0.1456], "color": [0.28, 0.26, 0.26, 1.0], "pivot": [-0.182, 0.4004, 0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 3.1416, "speed": 1},
        {"offset": [0.182, 0.182, 0.3276], "size": [0.1456, 0.364, 0.1456], "color": [0.28, 0.26, 0.26, 1.0], "pivot": [0.182, 0.4004, 0.3276], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 35, "phase": 0, "speed": 1},
        {"offset": [0.0, 0.728, 0.637], "size": [0.182, 0.182, 0.182], "color": [0.55, 0.55, 0.58, 1.0], "pivot": [0.0, 0.728, 0.546], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 10, "phase": 0, "speed": 2},
        {"offset": [0.0, 0.728, 0.8008], "size": [0.1638, 0.1638, 0.1456], "color": [0.18, 0.18, 0.2, 1.0], "pivot": [0.0, 0.728, 0.546], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 12, "phase": 0, "speed": 2},
        {"offset": [0.0, 0.728, 0.9464], "size": [0.1547, 0.1547, 0.1456], "color": [0.55, 0.55, 0.58, 1.0], "pivot": [0.0, 0.728, 0.546], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 14, "phase": 0, "speed": 2},
        {"offset": [0.0, 0.728, 1.092], "size": [0.1456, 0.1456, 0.1456], "color": [0.18, 0.18, 0.2, 1.0], "pivot": [0.0, 0.728, 0.546], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 16, "phase": 0, "speed": 2},
        {"offset": [0.0, 0.728, 1.2376], "size": [0.1274, 0.1274, 0.1092], "color": [0.08, 0.08, 0.1, 1.0], "pivot": [0.0, 0.728, 0.546], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 18, "phase": 0, "speed": 2},
    ],
}

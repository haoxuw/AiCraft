"""Dog — loyal companion, 4 legs, pointy ears, wagging tail.

Edit parts to customize the dog's appearance!

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "dog",
    "head_pivot": [0.0, 0.715, -0.3146],
    "walk_bob": 0.0357,
    "idle_bob": 0.0086,
    "walk_speed": 5.0,
    "parts": [
        {"name": "torso", "offset": [0.0, 0.5434, 0.0], "size": [0.5148, 0.4576, 1.001], "color": [0.75, 0.55, 0.35, 1.0]},
        {"name": "head", "offset": [0.0, 0.7436, -0.5434], "size": [0.4576, 0.4004, 0.4576], "color": [0.78, 0.58, 0.38, 1.0], "pivot": [0.0, 0.715, -0.3146], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.0, 0.6578, -0.7722], "size": [0.2288, 0.1716, 0.1716], "color": [0.7, 0.48, 0.3, 1.0], "pivot": [0.0, 0.715, -0.3146], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.0, 0.715, -0.8723], "size": [0.0858, 0.0572, 0.0286], "color": [0.08, 0.06, 0.05, 1.0], "pivot": [0.0, 0.715, -0.3146], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [-0.1144, 0.8008, -0.7793], "size": [0.0429, 0.0572, 0.0143], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.715, -0.3146], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [0.1144, 0.8008, -0.7793], "size": [0.0429, 0.0572, 0.0143], "color": [0.04, 0.03, 0.03, 1.0], "pivot": [0.0, 0.715, -0.3146], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 10, "phase": 0, "speed": 0.5, "head": True},
        {"offset": [-0.1787, 0.9438, -0.4862], "size": [0.1144, 0.2288, 0.1144], "color": [0.65, 0.42, 0.25, 1.0], "head": True},
        {"offset": [0.1787, 0.9438, -0.4862], "size": [0.1144, 0.2288, 0.1144], "color": [0.65, 0.42, 0.25, 1.0], "head": True},
        {"offset": [-0.143, 0.1716, -0.286], "size": [0.143, 0.4004, 0.143], "color": [0.72, 0.52, 0.32, 1.0], "pivot": [-0.143, 0.3718, -0.286], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 40, "phase": 0, "speed": 1},
        {"offset": [0.143, 0.1716, -0.286], "size": [0.143, 0.4004, 0.143], "color": [0.72, 0.52, 0.32, 1.0], "pivot": [0.143, 0.3718, -0.286], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 40, "phase": 3.1416, "speed": 1},
        {"offset": [-0.143, 0.1716, 0.286], "size": [0.143, 0.4004, 0.143], "color": [0.72, 0.52, 0.32, 1.0], "pivot": [-0.143, 0.3718, 0.286], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 40, "phase": 3.1416, "speed": 1},
        {"offset": [0.143, 0.1716, 0.286], "size": [0.143, 0.4004, 0.143], "color": [0.72, 0.52, 0.32, 1.0], "pivot": [0.143, 0.3718, 0.286], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 40, "phase": 0, "speed": 1},
        {"offset": [0.0, 0.6864, 0.5434], "size": [0.0858, 0.0858, 0.286], "color": [0.72, 0.52, 0.32, 1.0], "pivot": [0.0, 0.6435, 0.5005], "swing_axis": [0.0, 1.0, 0.0], "amplitude": 25, "phase": 0, "speed": 3},
        {"offset": [0.0, 0.6578, -0.4576], "size": [0.5434, 0.1144, 0.143], "color": [0.75, 0.15, 0.15, 1.0]},
        {"offset": [0.0, 0.572, -0.5577], "size": [0.0572, 0.0715, 0.0286], "color": [0.85, 0.7, 0.2, 1.0]},
        {"offset": [0.0, 0.4004, -0.5148], "size": [0.2002, 0.143, 0.0572], "color": [0.92, 0.9, 0.85, 1.0]},
    ],
}

"""Giant -- massive iron guardian.

Massive iron guardian with lava-cracked chest.
STR 5  STA 5  AGI 1  INT 1

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
Optional animation: pivot, swing_axis, amplitude (degrees), phase (radians), speed
"""

model = {
    "id": "giant",
    "hand_r": [0.72, 0.52, -0.2],
    "hand_l": [-0.72, 0.52, -0.2],
    "pivot_r": [0.48, 1.36, 0.0],
    "pivot_l": [-0.48, 1.36, 0.0],
    "walk_bob": 0.09,
    "idle_bob": 0.005,
    "walk_speed": 1.2,
    "parts": [
        {"offset": [0.0, 1.62, 0.0], "size": [0.6, 0.52, 0.52], "color": [0.42, 0.4, 0.38, 1.0], "pivot": [0.0, 1.46, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 3, "phase": 0, "speed": 1.5},
        {"offset": [-0.22, 0.28, 0.0], "size": [0.32, 0.56, 0.36], "color": [0.45, 0.42, 0.4, 1.0], "pivot": [-0.22, 0.58, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.22, 0.28, 0.0], "size": [0.32, 0.56, 0.36], "color": [0.45, 0.42, 0.4, 1.0], "pivot": [0.22, 0.58, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"offset": [-0.22, 0.05, 0.07], "size": [0.4, 0.16, 0.48], "color": [0.32, 0.3, 0.28, 1.0], "pivot": [-0.22, 0.58, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 0, "speed": 1},
        {"offset": [0.22, 0.05, 0.07], "size": [0.4, 0.16, 0.48], "color": [0.32, 0.3, 0.28, 1.0], "pivot": [0.22, 0.58, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 50, "phase": 3.1416, "speed": 1},
        {"offset": [0.0, 0.76, 0.0], "size": [0.8, 0.52, 0.6], "color": [0.45, 0.42, 0.4, 1.0]},
        {"offset": [0.0, 1.18, 0.0], "size": [0.84, 0.52, 0.56], "color": [0.48, 0.45, 0.43, 1.0]},
        {"offset": [0.0, 1.12, -0.295], "size": [0.24, 0.34, 0.02], "color": [1.0, 0.58, 0.08, 1.0]},
        {"offset": [0.0, 1.12, -0.305], "size": [0.12, 0.18, 0.02], "color": [1.0, 0.85, 0.3, 1.0]},
        {"offset": [-0.24, 0.92, -0.305], "size": [0.08, 0.08, 0.02], "color": [0.22, 0.2, 0.18, 1.0]},
        {"offset": [0.24, 0.92, -0.305], "size": [0.08, 0.08, 0.02], "color": [0.22, 0.2, 0.18, 1.0]},
        {"offset": [0.0, 0.92, -0.305], "size": [0.06, 0.06, 0.02], "color": [0.22, 0.2, 0.18, 1.0]},
        {"offset": [-0.48, 1.34, 0.0], "size": [0.1, 0.1, 0.1], "color": [0.22, 0.2, 0.18, 1.0]},
        {"offset": [0.48, 1.34, 0.0], "size": [0.1, 0.1, 0.1], "color": [0.22, 0.2, 0.18, 1.0]},
        {"name": "left_upper_arm", "offset": [-0.64, 1.0, 0.0], "size": [0.4, 0.84, 0.4], "color": [0.45, 0.42, 0.4, 1.0], "pivot": [-0.48, 1.36, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 48, "phase": 3.1416, "speed": 1},
        {"name": "left_hand", "offset": [-0.64, 0.52, 0.0], "size": [0.48, 0.4, 0.48], "color": [0.32, 0.3, 0.28, 1.0], "pivot": [-0.48, 1.36, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 48, "phase": 3.1416, "speed": 1},
        {"name": "right_upper_arm", "offset": [0.64, 1.0, 0.0], "size": [0.4, 0.84, 0.4], "color": [0.45, 0.42, 0.4, 1.0], "pivot": [0.48, 1.36, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 48, "phase": 0, "speed": 1},
        {"name": "right_hand", "offset": [0.64, 0.52, 0.0], "size": [0.48, 0.4, 0.48], "color": [0.32, 0.3, 0.28, 1.0], "pivot": [0.48, 1.36, 0.0], "swing_axis": [1.0, 0.0, 0.0], "amplitude": 48, "phase": 0, "speed": 1},
    ],
    "clips": {
        "attack": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 2.5},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 60, "phase": 0, "bias": 30, "speed": 2.5},
        },
        "chop": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.0},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 35, "phase": 0, "bias": 70, "speed": 1.0},
        },
        "mine": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.2},
            "right_hand": {"axis": [1.0, 0.0, 0.0], "amp": 40, "phase": 0, "bias": 60, "speed": 1.2},
        },
        "wave": {
            "right_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 1.8},
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 20, "phase": 0, "bias": 130, "speed": 1.8},
        },
        "dance": {
            "right_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.2},
            "right_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 0, "bias": 80, "speed": 1.2},
            "left_upper_arm": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.2},
            "left_hand": {"axis": [0.0, 0.0, 1.0], "amp": 35, "phase": 3.1416, "bias": -80, "speed": 1.2},
        },
        "sleep": {
            "right_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
            "left_upper_arm": {"axis": [1.0, 0.0, 0.0], "amp": 0, "phase": 0, "bias": 0, "speed": 0.5},
        },
    },
}

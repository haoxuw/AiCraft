"""Meat — raw drumstick dropped by animals on death.

A chunk of raw meat with a visible bone handle: large irregular meat body
at the top, pale bone shaft below, knobbed bone tip at the bottom.
Classic food-item silhouette readable at any scale.

Each part: offset=[x,y,z] (center), size=[w,h,d] (full size), color=[r,g,b,a]
"""

model = {
    "id": "meat",
    "equip": {"rotation": [15.0, 20.0, 5.0], "offset": [0.02, -0.06, -0.02]},
    "parts": [
        {"offset": [0.0, 0.234, 0.0], "size": [0.132, 0.132, 0.108], "color": [0.7, 0.18, 0.1, 1.0]},
        {"offset": [0.024, 0.288, 0.012], "size": [0.096, 0.084, 0.084], "color": [0.62, 0.14, 0.08, 1.0]},
        {"offset": [0.0, 0.24, -0.051], "size": [0.096, 0.084, 0.024], "color": [0.85, 0.48, 0.38, 1.0]},
        {"offset": [-0.03, 0.216, 0.042], "size": [0.042, 0.036, 0.03], "color": [0.88, 0.76, 0.6, 1.0]},
        {"offset": [0.0, 0.102, 0.0], "size": [0.042, 0.168, 0.042], "color": [0.88, 0.84, 0.75, 1.0]},
        {"offset": [0.0, 0.018, 0.0], "size": [0.078, 0.054, 0.066], "color": [0.84, 0.8, 0.7, 1.0]},
    ],
}

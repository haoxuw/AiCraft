"""Temperate weather schedule — Markov chain over clear/rain/snow/leaves.

The server's WeatherController advances this schedule each tick and
broadcasts (kind, intensity, wind) to all clients via S_WEATHER. Visuals
(sky tint, fog, particles) are 100% client-side (Rule 5).

States are weighted transitions — from "clear" there's a 70% chance of
staying clear, 20% of picking up leaves drift, 8% rain, 2% snow. Weights
don't need to sum to 1; the controller normalises.

mean_s is the expected seconds-in-state (exponentially sampled, clamped
to [0.2·mean_s, 3·mean_s] so the schedule never sticks or flaps).
intensity is a uniform pick in [min, max] applied on entry to the state.
"""

schedule = {
    "initial_kind": "clear",

    "kinds": [
        {
            "name":      "clear",
            "mean_s":    480.0,
            "intensity": [0.0, 0.0],
            "next": {
                "clear":  0.70,
                "leaves": 0.20,
                "rain":   0.08,
                "snow":   0.02,
            },
        },
        {
            "name":      "leaves",
            "mean_s":    180.0,
            "intensity": [0.3, 0.8],
            "next": {
                "clear":  0.70,
                "leaves": 0.20,
                "rain":   0.10,
            },
        },
        {
            "name":      "rain",
            "mean_s":    240.0,
            "intensity": [0.4, 1.0],
            "next": {
                "clear":  0.80,
                "rain":   0.15,
                "snow":   0.05,
            },
        },
        {
            "name":      "snow",
            "mean_s":    300.0,
            "intensity": [0.3, 0.9],
            "next": {
                "clear":  0.75,
                "snow":   0.15,
                "rain":   0.10,
            },
        },
    ],

    # Wind: low-frequency sinusoid around a base vector (blocks/sec, XZ).
    # Particles read wind to tilt their fall direction.
    "wind": {
        "base":          [0.3, 0.1],
        "noise_amp":     0.6,
        "noise_scale_s": 45.0,
    },
}

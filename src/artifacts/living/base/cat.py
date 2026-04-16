"""Cat — independent feline that chases chickens and naps."""

living = {
    "id": "cat",
    "name": "Cat",
    "description": "Independent feline that chases chickens and takes frequent naps.",

    "category": "animal",
    "playable": True,

    "model": "cat",
    "behavior": "prowl",

    # Physics
    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.5, 0.2]},
    "walk_speed": 3.5,
    "run_speed": 7.0,
    "gravity": 1.0,
    "eye_height": 0.45,
    "jump_velocity": 10.0,

    "primary_color": [0.90, 0.55, 0.20],
}

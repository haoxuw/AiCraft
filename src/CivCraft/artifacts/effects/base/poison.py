"""Poison — deals damage over time.

Applied by poison potions, spider bites, or traps.
"""

effect = {
    "id": "poison",
    "name": "Poison",
    "category": "debuff",
    "description": "Deals 1 damage every 2 seconds for the duration.",

    "target": "target",
    "damage_per_tick": 1,
    "tick_interval": 2.0,       # damage every 2 seconds
    "duration": 10.0,           # lasts 10 seconds

    "particles": "bubble",
    "sound": "poison_tick",
    "color": [0.2, 0.8, 0.1],  # toxic green
}

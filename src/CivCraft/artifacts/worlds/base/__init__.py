"""Built-in world template definitions.

Each world template is a Python dict describing terrain parameters,
village layout, mob spawns, and spawn placement.

The C++ engine reads these at startup to configure world generation.
Per-chunk terrain generation still runs in C++ for performance —
Python defines WHAT to generate, not HOW to generate it.

Players can fork any world template and customize it via the in-game
editor (Handbook → Worlds → Fork).
"""

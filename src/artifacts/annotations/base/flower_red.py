"""Flower (red) — a decorative annotation that sits on top of grass.

Annotations are render-only adornments attached to a block. They don't
occupy a block slot, have no collision, and never tick. See
shared/annotation.h for the C++ side.

Breaking the host block drops the flower as an item with the same id.
"""

annotation = {
    "id": "flower_red",
    "name": "Red Flower",
    "description": "A small red wildflower that grows on grass.",

    # Where the annotation sits relative to its host block.
    #   "top"    — above the block (flowers, mushrooms, grass tufts)
    #   "bottom" — hangs below       (stalactites, cobwebs, roots)
    #   "around" — wraps the block   (moss, vines, frost)
    "slot": "top",

    # Where it can naturally spawn. World-gen scatters annotations on any
    # top-facing block whose string id matches one of these.
    "spawn_on": ["grass"],
    "spawn_chance": 0.04,  # per-grass-top roll during chunk gen

    "model": "flower_red",
}

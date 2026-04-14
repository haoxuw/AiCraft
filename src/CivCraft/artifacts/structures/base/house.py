blueprint = {
    "id": "house",
    "display_name": "House",
    "regenerates": True,
    "regen_interval_s": 60,
    "anchor": {
        "block_type": "planks",
        "offset": [0, -1, 0],
        "hardness": 16,
    },
    "blocks": [
        # Populated dynamically per house template during world gen.
        # This is the base definition; actual block lists vary by house layout.
    ],
}

"""NAND Gate — small gray logic block with colored indicator dots.

The universal logic gate. Two inputs, one output.
"""

model = {
    "id": "nand_gate",
    "height": 0.3,
    "parts": [
        # Gate body — small gray box
        {"offset": [0, 0.06, 0], "size": [0.30, 0.12, 0.20], "color": [0.45, 0.45, 0.48, 1]},
        # Input dots — two red indicators on one side
        {"offset": [-0.08, 0.10, 0.10], "size": [0.04, 0.04, 0.02], "color": [0.85, 0.20, 0.15, 1]},
        # Output dot — green indicator on opposite side
        {"offset": [0.08, 0.10, -0.10], "size": [0.04, 0.04, 0.02], "color": [0.20, 0.75, 0.25, 1]},
    ]
}

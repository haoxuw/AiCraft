"""NAND Gate — small gray logic block with colored indicator dots.

The universal logic gate. Two inputs, one output.
"""

model = {
    "id": "nand_gate",
    "parts": [
        {"offset": [0.0, 0.06, 0.0], "size": [0.3, 0.12, 0.2], "color": [0.45, 0.45, 0.48, 1.0]},
        {"offset": [-0.08, 0.1, 0.1], "size": [0.04, 0.04, 0.02], "color": [0.85, 0.2, 0.15, 1.0]},
        {"offset": [0.08, 0.1, -0.1], "size": [0.04, 0.04, 0.02], "color": [0.2, 0.75, 0.25, 1.0]},
    ],
}

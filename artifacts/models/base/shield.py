"""Shield — wooden shield with metal boss."""

model = {
    "id": "shield",
    "height": 0.8,
    "parts": [
        # Face — flat wooden board
        {"offset": [0, 0.30, 0], "size": [0.04, 0.44, 0.36], "color": [0.45, 0.30, 0.15, 1]},
        # Boss — center metal bump
        {"offset": [0.02, 0.30, 0], "size": [0.04, 0.16, 0.16], "color": [0.55, 0.50, 0.40, 1]},
        # Rim — edge banding
        {"offset": [0, 0.30, 0], "size": [0.05, 0.46, 0.02], "color": [0.50, 0.45, 0.35, 1]},
    ]
}

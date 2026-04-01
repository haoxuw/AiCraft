"""Bucket — picks up and places water."""

item = {
    "id": "base:bucket",
    "name": "Bucket",
    "category": "tool",
    "stack_max": 1,
    "cooldown": 0.3,

    "on_use": "bucket_interact",    # pick up water → water bucket, place water bucket → water block
    "range": 4.0,

    "model": "bucket",
    "color": [0.6, 0.6, 0.6],
}

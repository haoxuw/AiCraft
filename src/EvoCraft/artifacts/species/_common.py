"""Helpers shared across species modules — pure functions, no state.

The C++ server imports each species.py by module name; this module is a
plain neighbour on sys.path so every species can `from _common import ...`.

Spore Cell Stage runs on the XZ plane (Y is fixed at 0). Position helpers
read x/z and ignore y entirely.
"""

import math


def heading_to(cell, tx, tz):
	"""Angle (radians) from cell toward point (tx, tz) in the XZ plane."""
	return math.atan2(tz - cell["z"], tx - cell["x"])


def nearest(cell, items):
	"""Return (item, dist_sq) for the closest item, or (None, inf)."""
	best = None
	best_d = float("inf")
	for it in items:
		dx = it["x"] - cell["x"]
		dz = it["z"] - cell["z"]
		d = dx * dx + dz * dz
		if d < best_d:
			best_d = d
			best = it
	return best, best_d


def wrap(a):
	while a >  math.pi: a -= 2 * math.pi
	while a < -math.pi: a += 2 * math.pi
	return a

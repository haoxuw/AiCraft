"""Dodger — flees from the nearest OTHER cell.

Behaviour:
  * If another cell is within ~6 units, swim directly away at full speed.
  * Otherwise pick a slow random drift target so it still looks alive.

A common emergent result: seekers chase food, dodgers scatter when a seeker
approaches — giving the slab visible personality without any scripting
beyond three tiny Python files.
"""

import math
import random

from _common import heading_to, nearest, wrap


_FLEE_RADIUS_SQ = 6.0 * 6.0


def decide_batch(cells, food, others):
	out = []
	for c in cells:
		threat, d2 = nearest(c, others)
		if threat is not None and d2 < _FLEE_RADIUS_SQ:
			away = heading_to(c, threat["x"], threat["z"]) + math.pi
			out.append((wrap(away), 1.0))
		else:
			# Slow meander — nudge current heading by a tiny random delta.
			out.append((c["angle"] + random.uniform(-0.4, 0.4), 0.55))
	return out

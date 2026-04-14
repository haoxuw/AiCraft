"""Seeker — chases the nearest food pellet.

Behaviour:
  * If any food exists, head straight for the closest one at full speed.
  * Otherwise coast forward, with a tiny drift, at half speed.

The C++ server calls decide_batch(cells, food, others) every sim tick. We
return (desired_angle_rad, speed_mul 0..1) per cell in the same order.
"""

from _common import heading_to, nearest


def decide_batch(cells, food, others):
	out = []
	for c in cells:
		target, _ = nearest(c, food)
		if target is not None:
			out.append((heading_to(c, target["x"], target["z"]), 1.0))
		else:
			out.append((c["angle"], 0.5))
	return out

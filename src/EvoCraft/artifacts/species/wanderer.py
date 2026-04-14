"""Wanderer — random walk on heading, indifferent to food and neighbours.

This is the "default" species — a simple baseline you can copy as a
starting point for new behaviours.
"""

import random


def decide_batch(cells, food, others):
	out = []
	for c in cells:
		# Small random drift on heading; speed varies gently per-cell.
		drift = random.uniform(-0.35, 0.35)
		out.append((c["angle"] + drift, 0.7))
	return out

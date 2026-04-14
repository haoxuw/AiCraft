# ChalkLife — main scene controller.
#
# Sets the chalkboard color, spawns a handful of creatures at random
# positions with distinct seeds (so each one has unique handwriting),
# and arranges the post-process overlay on top.
#
# Spawn count + seeds are here rather than in the scene file so
# iteration is faster — edit this file, hit play, no scene reload.

extends Node2D

const ChalkCreature = preload("res://scripts/chalk_creature.gd")

const NUM_CREATURES := 5
const MARGIN := 120.0

func _ready() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	var vp := get_viewport_rect().size
	for i in range(NUM_CREATURES):
		var c: Node2D = ChalkCreature.new()
		c.creature_seed = float(i) * 11.3 + 1.7
		c.body_radius = rng.randf_range(26.0, 42.0)
		c.leg_length = c.body_radius * rng.randf_range(0.7, 1.1)
		c.gait_hz = rng.randf_range(1.0, 1.8)
		c.position = Vector2(
			rng.randf_range(MARGIN, vp.x - MARGIN),
			rng.randf_range(MARGIN, vp.y - MARGIN))
		add_child(c)

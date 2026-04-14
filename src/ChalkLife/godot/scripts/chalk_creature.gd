# ChalkLife — a single chalk-drawn creature.
#
# Body plan: round body + two eyes + mouth + four legs. All parts
# drawn as wobbly chalk strokes via ChalkPen. Legs animate via a
# 4-phase gait; body breathes (radius pulse); eyes blink occasionally;
# whole creature ambles across the board on a sine path.
#
# Intentionally kept as one class with parameters rather than
# polymorphic subclasses — Stage 2 will introduce a real part system;
# this is the visual smoke test.

extends Node2D

const ChalkPen = preload("res://scripts/chalk_pen.gd")

@export var body_radius: float = 36.0
@export var leg_length: float = 28.0
@export var gait_hz: float = 1.4
@export var wander_speed: float = 32.0
@export var creature_seed: float = 7.0

var _t: float = 0.0
var _blink_timer: float = 2.0
var _blink_amt: float = 0.0

func _ready() -> void:
	set_process(true)

func _process(dt: float) -> void:
	_t += dt

	# Ambling motion — a meander across the board.
	position.x += cos(_t * 0.4 + creature_seed) * wander_speed * dt
	position.y += sin(_t * 0.27 + creature_seed * 1.3) * wander_speed * 0.55 * dt

	# Blink: close rapidly then open. `_blink_amt` is 1.0 during a blink.
	_blink_timer -= dt
	if _blink_timer <= 0.0:
		_blink_amt = 1.0
		_blink_timer = randf_range(3.0, 6.5)
	_blink_amt = max(0.0, _blink_amt - dt * 6.0)

	queue_redraw()

func _draw() -> void:
	# Breathing: radius pulses ~6%.
	var breath := 1.0 + sin(_t * 2.0) * 0.06
	var r := body_radius * breath

	# Body — seeded so the wobble is stable per-frame (creature doesn't
	# look like it's dissolving). The running `_t` is NOT folded into
	# the seed; we want the outline to stay put while only specific
	# things (legs, blink) animate.
	ChalkPen.circle(self, Vector2.ZERO, r, creature_seed, 2.4, 36, 1.3)

	_draw_legs(r)
	_draw_face(r)

func _draw_legs(r: float) -> void:
	# Four legs, two per side, phase-offset so they look like a crawl.
	var phases: Array[float] = [0.0, PI, PI * 0.5, PI * 1.5]
	var hips: Array[Vector2] = [
		Vector2(-r * 0.7,  r * 0.45),
		Vector2( r * 0.7,  r * 0.45),
		Vector2(-r * 0.55, r * 0.70),
		Vector2( r * 0.55, r * 0.70),
	]
	for i in range(4):
		var ph: float = phases[i]
		var hip: Vector2 = hips[i]
		# Foot swings fore/aft + lifts when airborne.
		var swing: float = sin(_t * TAU * gait_hz + ph)
		var lift_raw: float = cos(_t * TAU * gait_hz + ph)
		var lift: float = maxf(0.0, lift_raw) * leg_length * 0.35
		var foot: Vector2 = hip + Vector2(swing * leg_length * 0.5,
		                                  leg_length - lift)
		ChalkPen.line(self, hip, foot, creature_seed + float(i) * 3.3,
		              2.2, 6, 1.0)

func _draw_face(r: float) -> void:
	var eye_l := Vector2(-r * 0.32, -r * 0.15)
	var eye_r := Vector2( r * 0.32, -r * 0.15)
	var eye_r_px := r * 0.11

	# Blink: squash eye height to a short arc instead of a circle.
	if _blink_amt > 0.1:
		# Closed-eye dash.
		var half_w := eye_r_px
		ChalkPen.line(self, eye_l - Vector2(half_w, 0), eye_l + Vector2(half_w, 0),
		              creature_seed + 101.0, 2.0, 4, 0.5)
		ChalkPen.line(self, eye_r - Vector2(half_w, 0), eye_r + Vector2(half_w, 0),
		              creature_seed + 113.0, 2.0, 4, 0.5)
	else:
		ChalkPen.circle(self, eye_l, eye_r_px, creature_seed + 101.0, 2.0, 14, 0.7)
		ChalkPen.circle(self, eye_r, eye_r_px, creature_seed + 113.0, 2.0, 14, 0.7)
		# Pupils — small chalk dots tracking forward motion slightly.
		var look := Vector2(cos(_t * 0.4 + creature_seed) * eye_r_px * 0.35, 0.0)
		ChalkPen.dot(self, eye_l + look, 1.8, creature_seed + 131.0)
		ChalkPen.dot(self, eye_r + look, 1.8, creature_seed + 137.0)

	# Mouth — a friendly arc under the eyes, curling up a hair with the
	# breath so the creature looks like it's smiling on exhale.
	var mouth_y := r * 0.30
	var curl := -0.18 + sin(_t * 2.0) * 0.05
	ChalkPen.arc(self, Vector2(0, mouth_y), r * 0.35,
	             PI * (0.15 + curl), PI * (0.85 - curl),
	             creature_seed + 211.0, 2.0, 14, 0.8)

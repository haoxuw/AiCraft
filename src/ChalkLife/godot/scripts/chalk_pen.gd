# ChalkLife — chalk pen primitives
#
# A tiny library of draw helpers that any creature can call from its
# own _draw(). The helpers introduce small per-vertex wobble so a
# "circle" isn't a perfect circle — it's a hand-drawn chalk circle.
#
# Pure drawing primitives. No game state, no signals. Call these from
# inside a Node2D._draw() override.

class_name ChalkPen
extends RefCounted

const CHALK_COLOR := Color(0.95, 0.94, 0.88, 1.0)
const STROKE_WIDTH := 2.2

# Seed-based deterministic wobble so the same creature doesn't jitter
# into visual noise frame-to-frame. The caller supplies `seed` — use
# e.g. the creature's id so every creature has its own handwriting.
static func _wob(sd: float, k: float, amp: float) -> Vector2:
	var a: float = sin(sd * 12.9898 + k * 78.233) * 43758.5453
	var b: float = sin(sd * 93.989  + k * 47.417) * 28375.123
	return Vector2(fposmod(a, 2.0) - 1.0, fposmod(b, 2.0) - 1.0) * amp

# --- Stroke: wobbly straight line from a to b ------------------------
static func line(node: CanvasItem, a: Vector2, b: Vector2,
                 seed: float = 0.0, width: float = STROKE_WIDTH,
                 segments: int = 8, wobble: float = 1.6) -> void:
	var prev := a
	for i in range(1, segments + 1):
		var t := float(i) / float(segments)
		var mid := a.lerp(b, t)
		if i < segments:
			mid += _wob(seed, t * 3.7, wobble)
		node.draw_line(prev, mid, CHALK_COLOR, width, true)
		prev = mid

# --- Stroke: wobbly circle ------------------------------------------
static func circle(node: CanvasItem, center: Vector2, radius: float,
                   seed: float = 0.0, width: float = STROKE_WIDTH,
                   segments: int = 28, wobble: float = 1.2) -> void:
	var prev := center + Vector2(radius, 0.0)
	for i in range(1, segments + 1):
		var ang := TAU * float(i) / float(segments)
		var r := radius + _wob(seed, ang * 0.35, wobble).x
		var p := center + Vector2(cos(ang) * r, sin(ang) * r)
		node.draw_line(prev, p, CHALK_COLOR, width, true)
		prev = p

# --- Stroke: wobbly arc (from angle a0 to a1, radians) --------------
static func arc(node: CanvasItem, center: Vector2, radius: float,
                a0: float, a1: float,
                seed: float = 0.0, width: float = STROKE_WIDTH,
                segments: int = 14, wobble: float = 1.0) -> void:
	var prev := center + Vector2(cos(a0) * radius, sin(a0) * radius)
	for i in range(1, segments + 1):
		var t := float(i) / float(segments)
		var ang: float = lerpf(a0, a1, t)
		var r := radius + _wob(seed, ang * 0.41 + t, wobble).x
		var p := center + Vector2(cos(ang) * r, sin(ang) * r)
		node.draw_line(prev, p, CHALK_COLOR, width, true)
		prev = p

# --- Stroke: filled chalk dot (small scribble) ----------------------
static func dot(node: CanvasItem, center: Vector2, radius: float = 3.0,
                seed: float = 0.0) -> void:
	# "Fill" as a few concentric wobbly rings — reads as a chalk smudge
	# without needing a fill primitive.
	for k in range(3):
		circle(node, center, radius * (1.0 - k * 0.25), seed + k * 7.1,
		       STROKE_WIDTH * 0.8, 14, 0.6)

# CellCraft — ChalkStroke
#
# A single chalk-line visual. Port of NumptyPhysics' feathered-ribbon
# technique (platform/gl/GLRenderer.cpp:757-835) using Godot's Line2D
# + a transparent→opaque→transparent gradient across the width.
#
# That gradient is the whole trick: the line looks like chalk because
# its edges fade to zero alpha, giving the soft blurred border. No
# shader needed.
#
# Physics is NOT in this class. ChalkStroke is pure visuals; the
# physics body is built elsewhere so the same stroke data can be
# rendered on client without running physics.

class_name ChalkStroke
extends Line2D

# NumptyPhysics palette — hex int → Color. First entry is the default.
# Index order matches NumptyPhysics (src/Colour.cpp:21-34) so save
# files are interchangeable if we ever want to import their levels.
const PALETTE: Array[Color] = [
	Color8(0xb8, 0x00, 0x00),  # red     — RED
	Color8(0xee, 0xc9, 0x00),  # yellow  — YELLOW
	Color8(0x00, 0x00, 0x77),  # blue    — DEFAULT
	Color8(0x10, 0x87, 0x10),  # green
	Color8(0x10, 0x10, 0x10),  # black
	Color8(0x8b, 0x45, 0x13),  # brown
	Color8(0x87, 0xce, 0xfa),  # lightblue
	Color8(0xee, 0x6a, 0xa7),  # pink
	Color8(0xb2, 0x3a, 0xee),  # purple
	Color8(0x00, 0xfa, 0x9a),  # lightgreen
	Color8(0xff, 0x7f, 0x00),  # orange
	Color8(0x6c, 0x7b, 0x8b),  # grey
]

# NumptyPhysics uses w=0.9 (core) + e=1.0 (feather) = 3.8 px total.
# We match that: core 2 px, total ~4 px with feathering on the sides.
const CHALK_WIDTH: float = 4.0

# Douglas-Peucker threshold when simplifying the raw mouse polyline.
# NumptyPhysics: src/Config.h:33 SIMPLIFY_THRESHOLDf = 1.0 px.
const SIMPLIFY_THRESHOLD: float = 1.0

static var _chalk_gradient: Gradient = _make_chalk_gradient()

static func _make_chalk_gradient() -> Gradient:
	# Line2D interprets `gradient` along the *width* of the ribbon
	# (v=0 at one edge, 1 at the other). We want opaque in the middle
	# and fully transparent at both edges — feathered chalk mark.
	var g := Gradient.new()
	g.set_offset(0, 0.00); g.set_color(0, Color(1, 1, 1, 0.00))
	g.add_point(0.18, Color(1, 1, 1, 1.00))
	g.add_point(0.82, Color(1, 1, 1, 1.00))
	g.add_point(1.00, Color(1, 1, 1, 0.00))
	return g

# Configure a stroke with a color index and the raw polyline from
# input. Caller passes points in local coordinates of this Line2D's
# parent — no internal transform applied here.
func setup(color_index: int, raw_points: PackedVector2Array) -> void:
	default_color = PALETTE[clampi(color_index, 0, PALETTE.size() - 1)]
	width = CHALK_WIDTH
	gradient = _chalk_gradient
	joint_mode = Line2D.LINE_JOINT_ROUND
	begin_cap_mode = Line2D.LINE_CAP_ROUND
	end_cap_mode = Line2D.LINE_CAP_ROUND
	antialiased = true
	points = simplify(raw_points, SIMPLIFY_THRESHOLD)

# --- Douglas-Peucker polyline simplification ------------------------
# Removes points whose perpendicular distance to the chord between
# their neighbours is below `eps`. Keeps sharp corners, drops noise
# from high-frequency mouse samples. NumptyPhysics does this before
# building physics bodies; we do it before building the visual so the
# rendered line matches the simulated one exactly.
static func simplify(pts: PackedVector2Array, eps: float) -> PackedVector2Array:
	if pts.size() < 3:
		return pts
	return _dp(pts, 0, pts.size() - 1, eps)

static func _dp(pts: PackedVector2Array, i0: int, i1: int, eps: float) -> PackedVector2Array:
	var max_d: float = 0.0
	var idx: int = -1
	var a: Vector2 = pts[i0]
	var b: Vector2 = pts[i1]
	for i in range(i0 + 1, i1):
		var d: float = _perp_dist(pts[i], a, b)
		if d > max_d:
			max_d = d
			idx = i
	if max_d > eps and idx != -1:
		var left: PackedVector2Array = _dp(pts, i0, idx, eps)
		var right: PackedVector2Array = _dp(pts, idx, i1, eps)
		# Drop the duplicated mid-point from the right half.
		left.remove_at(left.size() - 1)
		return left + right
	else:
		var out: PackedVector2Array = PackedVector2Array()
		out.append(a); out.append(b)
		return out

static func _perp_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab: Vector2 = b - a
	var len2: float = ab.length_squared()
	if len2 < 0.0001:
		return p.distance_to(a)
	var t: float = clampf(((p - a).dot(ab)) / len2, 0.0, 1.0)
	return p.distance_to(a + ab * t)

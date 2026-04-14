# LifeCraft — DrawingCanvas
#
# Captures mouse input, builds a polyline while the button is held,
# spawns a ChalkStroke on release. Purely client-visual for v0; later
# the stroke data is also sent to the server (C_STROKE message) so
# the authoritative sim can turn it into a physics body.

extends Node2D

const ChalkStroke = preload("res://scripts/shared/chalk_stroke.gd")

# Min distance between sampled points. Mouse events often fire at sub-
# pixel rates; sampling every pixel makes DP-simplification work
# harder without improving the result.
const SAMPLE_MIN_DIST: float = 2.0

@export var current_color_index: int = 2   # DEFAULT (blue) to match NumptyPhysics

var _drawing: bool = false
var _current_points: PackedVector2Array = PackedVector2Array()
var _live_preview: Line2D

func _ready() -> void:
	set_process_input(true)

func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb: InputEventMouseButton = event
		if mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.pressed:
				_begin_stroke(mb.position)
			else:
				_end_stroke()
		elif mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			# Cycle colors for quick iteration. Temporary input until
			# the UI lands.
			current_color_index = (current_color_index + 1) % ChalkStroke.PALETTE.size()
	elif event is InputEventMouseMotion and _drawing:
		var pos: Vector2 = (event as InputEventMouseMotion).position
		if _current_points.is_empty() or _current_points[-1].distance_to(pos) >= SAMPLE_MIN_DIST:
			_current_points.append(pos)
			_live_preview.points = _current_points

func _begin_stroke(at: Vector2) -> void:
	_drawing = true
	_current_points = PackedVector2Array([at])
	_live_preview = ChalkStroke.new()
	_live_preview.setup(current_color_index, _current_points)
	add_child(_live_preview)

func _end_stroke() -> void:
	if not _drawing:
		return
	_drawing = false
	# Finalize: re-run setup with the full point set so the stroke is
	# DP-simplified. The live-preview version rendered raw samples.
	if _current_points.size() >= 2:
		_live_preview.setup(current_color_index, _current_points)
	else:
		_live_preview.queue_free()
	_live_preview = null
	_current_points = PackedVector2Array()

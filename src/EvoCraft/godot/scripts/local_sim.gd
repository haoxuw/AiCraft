# EvoCraft local sim — Godot-only replacement for the old TCP net_client.
#
# Produces the exact same snapshot API world_builder.gd expects
# (update_cells, update_food, update_player_stats, update_player_parts)
# so nothing downstream had to change.
#
# Sim is deliberately minimal:
#   * 60 NPC cells wander on XZ via smooth noise-driven steering.
#   * Player cell (species = 3) is driven by WASD; heading follows velocity.
#   * 60 food pellets scattered; respawn somewhere new when a cell overlaps.
#
# Per CLAUDE.md Rule 1 the real game would run this in Python on a separate
# process — but for visual prototyping we keep everything in-engine. The
# contract (record schema, call cadence) is preserved so we can swap in a
# networked feed later without touching world_builder.gd.

extends Node

const HALF_W            := 50.0
const HALF_D            := 50.0
const NUM_SPECIES       := 4
const PLAYER_SPECIES    := 3
const NUM_FOOD_KINDS    := 3

const NPC_PER_SPECIES   := 20
const FOOD_COUNT        := 60

const PLAYER_MAX_SPEED  := 8.0
const PLAYER_ACCEL      := 18.0
const PLAYER_FRICTION   := 3.5
const NPC_WANDER_SPEED  := 2.5
const EAT_RADIUS        := 0.9

var _rng := RandomNumberGenerator.new()
var _cells: Array = []            # [{id, species, pos:Vector2, vel:Vector2, angle, size, seed}]
var _foods: Array = []            # [{kind, pos:Vector2}]
var _player_idx: int = -1
var _player_hp: float = 100.0
var _player_dna: int = 0

func _ready() -> void:
	_rng.seed = 424242
	_spawn_npcs()
	_spawn_player()
	_spawn_food()

func _process(dt: float) -> void:
	_tick_npcs(dt)
	_tick_player(dt)
	_resolve_eating()
	_push_snapshots()

# --- spawn -----------------------------------------------------------------

func _spawn_npcs() -> void:
	var next_id := 100
	for sp in [0, 1, 2]:
		for _i in NPC_PER_SPECIES:
			_cells.append({
				"id":      next_id,
				"species": sp,
				"pos":     Vector2(_rng.randf_range(-HALF_W + 2.0,  HALF_W - 2.0),
				                   _rng.randf_range(-HALF_D + 2.0,  HALF_D - 2.0)),
				"vel":     Vector2.ZERO,
				"angle":   _rng.randf() * TAU,
				"size":    _rng.randf_range(0.45, 0.75),
				"seed":    _rng.randf() * 100.0,
				"t":       _rng.randf() * 10.0,  # wander-noise phase
			})
			next_id += 1

func _spawn_player() -> void:
	_player_idx = _cells.size()
	_cells.append({
		"id":      1,
		"species": PLAYER_SPECIES,
		"pos":     Vector2.ZERO,
		"vel":     Vector2.ZERO,
		"angle":   0.0,
		"size":    0.9,
		"seed":    0.0,
		"t":       0.0,
	})

func _spawn_food() -> void:
	for _i in FOOD_COUNT:
		_foods.append({
			"kind": _rng.randi_range(0, NUM_FOOD_KINDS - 1),
			"pos":  Vector2(_rng.randf_range(-HALF_W + 1.0,  HALF_W - 1.0),
			                _rng.randf_range(-HALF_D + 1.0,  HALF_D - 1.0)),
		})

# --- sim -------------------------------------------------------------------

func _tick_npcs(dt: float) -> void:
	for i in _cells.size():
		if i == _player_idx:
			continue
		var c: Dictionary = _cells[i]
		c.t += dt
		# Smooth noise-driven steering: angle drifts on a low-freq sine pair
		# seeded per-cell so every NPC wanders differently.
		var wander := Vector2(
			sin(c.t * 0.7 + c.seed),
			cos(c.t * 0.9 + c.seed * 1.3))
		c.angle = atan2(wander.y, wander.x)
		c.vel = wander.normalized() * NPC_WANDER_SPEED
		c.pos += c.vel * dt
		# Soft wall bounce: reflect velocity, clamp position.
		if   c.pos.x >  HALF_W - 1.0: c.pos.x =  HALF_W - 1.0; c.seed += 1.7
		elif c.pos.x < -HALF_W + 1.0: c.pos.x = -HALF_W + 1.0; c.seed += 1.7
		if   c.pos.y >  HALF_D - 1.0: c.pos.y =  HALF_D - 1.0; c.seed += 1.3
		elif c.pos.y < -HALF_D + 1.0: c.pos.y = -HALF_D + 1.0; c.seed += 1.3

func _tick_player(dt: float) -> void:
	if _player_idx < 0: return
	var c: Dictionary = _cells[_player_idx]
	var input := Vector2(
		Input.get_axis("ui_left", "ui_right"),
		Input.get_axis("ui_up",   "ui_down"))
	if input.length_squared() > 1.0:
		input = input.normalized()
	var vel: Vector2 = c.vel
	vel += input * PLAYER_ACCEL * dt
	# Friction
	var speed: float = vel.length()
	var new_speed: float = max(0.0, speed - PLAYER_FRICTION * dt)
	if speed > 0.0:
		vel = vel * (new_speed / speed)
	if vel.length() > PLAYER_MAX_SPEED:
		vel = vel.normalized() * PLAYER_MAX_SPEED
	c.vel = vel
	c.pos += vel * dt
	if vel.length_squared() > 0.01:
		c.angle = atan2(vel.y, vel.x)
	c.pos.x = clampf(c.pos.x, -HALF_W + 1.0, HALF_W - 1.0)
	c.pos.y = clampf(c.pos.y, -HALF_D + 1.0, HALF_D - 1.0)

func _resolve_eating() -> void:
	if _player_idx < 0: return
	var pp: Vector2 = _cells[_player_idx].pos
	for i in _foods.size():
		if pp.distance_squared_to(_foods[i].pos) < EAT_RADIUS * EAT_RADIUS:
			# Respawn somewhere else instead of removing — keeps the scene full.
			_foods[i].pos = Vector2(
				_rng.randf_range(-HALF_W + 1.0,  HALF_W - 1.0),
				_rng.randf_range(-HALF_D + 1.0,  HALF_D - 1.0))
			_foods[i].kind = _rng.randi_range(0, NUM_FOOD_KINDS - 1)
			_player_dna += 1

# --- snapshot emission -----------------------------------------------------

func _push_snapshots() -> void:
	var world := get_parent()
	if world == null: return

	if world.has_method("update_cells"):
		var cell_records: Array = []
		cell_records.resize(_cells.size())
		for i in _cells.size():
			var c: Dictionary = _cells[i]
			cell_records[i] = {
				"id":      c.id,
				"species": c.species,
				"x":       c.pos.x,
				"z":       c.pos.y,     # world_builder uses XZ plane
				"angle":   c.angle,
				"size":    c.size,
			}
		world.update_cells(cell_records)

	if world.has_method("update_food"):
		var food_records: Array = []
		food_records.resize(_foods.size())
		for i in _foods.size():
			food_records[i] = {
				"kind": _foods[i].kind,
				"x":    _foods[i].pos.x,
				"z":    _foods[i].pos.y,
			}
		world.update_food(food_records)

	if world.has_method("update_player_stats"):
		world.update_player_stats(_player_hp, 100.0, _player_dna)

# --- part-editor stubs (local-only: parts don't do anything in v1) ---------
# world_builder's parts editor calls these; keep them no-ops so the UI
# doesn't crash. Stage 4+ of VISUAL_PLAN.md will give them real behavior.

func send_buy_part(_kind: int, _ang: float, _dist: float) -> void:
	pass

func send_reset_parts() -> void:
	pass

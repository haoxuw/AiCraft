# Spore Cell Stage scene composer.
#
# Top-down petri dish: camera looks straight down at the player from above
# (orthographic so the dish reads as a microscope view). The simulation runs
# on the XZ plane at Y=0 — there is no depth to swim through.
#
# Per-species microbe morphology is built procedurally as small flat-ish
# meshes with each species' own silhouette:
#   * 0 amoeba      irregular blob, slightly lumpy edge
#   * 1 ciliate     oval body ringed with cilia spikes
#   * 2 flagellate  teardrop body with a trailing flagellum
#   * 3 player      vibrant green amoeba; chevron heading marker on top

extends Node3D

const HALF_W            := 50.0      # matches swim_slab.h HALF_W
const HALF_D            := 50.0      # matches swim_slab.h HALF_D
const NUM_SPECIES       := 4         # 0..2 NPC microbes, 3 = player
const PLAYER_SPECIES    := 3
const CELL_CAPACITY     := 96
const FOOD_CAPACITY     := 96
const CAMERA_HEIGHT     := 18.0
const CAMERA_DISTANCE   := 16.0   # back offset from player on +Z so the tilt reveals depth
const CAMERA_TILT_DEG   := -48.0  # -90 = straight-down (flat); -48 ≈ Spore cell-stage perch
const CAMERA_FOV_DEG    := 45.0

# --- species shape catalog --------------------------------------------------
#
# Each microbe is a full 3D ellipsoid with explicit (length, width, height)
# — L along +X (forward), W along Z (side), H along Y (up/down). Shapes are
# symmetrical (no per-vertex jitter) so they read as clean geometric bodies,
# not lumpy blobs. Tessellation (segments × rings) is bumped up on the player
# for a smoother primary read.
const SHAPE_LENGTH   := 0
const SHAPE_WIDTH    := 1
const SHAPE_HEIGHT   := 2
const SHAPE_SEGMENTS := 3
const SHAPE_RINGS    := 4

const SPECIES_SHAPE := [
	# 0 amoeba — near-round, slightly squashed for that blob read
	{"length": 1.00, "width": 1.00, "height": 0.85, "segments": 22, "rings": 12},
	# 1 ciliate — elongated + flattened; the ring of cilia reads best on a
	# low-profile body so they don't get lost in the dome
	{"length": 1.35, "width": 0.80, "height": 0.55, "segments": 22, "rings": 10},
	# 2 flagellate — teardrop: longer than ciliate, same flat-ish height so
	# the trailing tail stays prominent
	{"length": 1.25, "width": 0.80, "height": 0.60, "segments": 22, "rings": 10},
	# 3 player — rounder & larger tessellation for a hero cell read
	{"length": 1.10, "width": 1.10, "height": 0.95, "segments": 26, "rings": 14},
]

# World-space Y offset applied to every cell when rendering. Server physics
# stays on the Y=0 plane (2D sim) but cells visually float above the dish
# floor so their full ellipsoid volume is visible from the tilted camera.
const CELL_Y_FLOAT := 0.15   # additive lift so the cell bottom clears the floor

var _species_mm: Array = []          # MultiMesh per species
var _food_mms: Array = []            # MultiMesh per food kind (plant/meat/egg)
var _cell_seeds: Dictionary = {}

const FOOD_KIND_PLANT := 0
const FOOD_KIND_MEAT  := 1
const FOOD_KIND_EGG   := 2
const NUM_FOOD_KINDS  := 3
var _camera: Camera3D
var _chevron: MeshInstance3D
var _player_pos: Vector3 = Vector3.ZERO
var _player_angle: float = 0.0

# Static decor — real geometry sprinkled through the dish. Bubbles drift
# slowly along +Z (wrap) to suggest a current; crystals sit and spin in
# place. Both are pure visual; the server has no idea they exist and
# the player can't interact with them.
var _bubbles: Array = []   # of MeshInstance3D
var _bubble_drift: Array = []  # parallel array of Vector2 (vx, vz) speeds
var _crystals: Array = []  # of MeshInstance3D
var _crystal_spin: Array = []  # parallel array of float (rad/s)

# Spore-style background leviathans — enormous (much larger than viewport)
# blurry silhouettes drifting far behind the play plane. Barely visible, just
# murky clumps of other organisms to sell the "drop of pond water teeming with
# life" feel. Pure visual decor; server doesn't know they exist.
var _leviathans: Array = []        # of MeshInstance3D
var _leviathan_drift: Array = []   # parallel array of Vector2 (vx, vz)
var _leviathan_spin: Array = []    # parallel array of float (rad/s, around Y)

# Player size, sniffed from update_cells. Editor click-to-place needs this to
# convert world distance into the [0,1] radial fraction the server expects.
var _player_size: float = 2.4

# Parts visualization — child of the world; rebuilt from scratch every time
# the server sends a new S_PLAYER_PARTS snapshot. Cheap because part lists
# are small (<24) and snapshots are rare.
var _parts_root: Node3D
var _parts_records: Array = []

# Editor state
var _editor_open: bool = false
var _editor_layer: CanvasLayer
var _editor_panel: Panel
var _editor_status: Label
var _editor_help: Label
var _placement_kind: int = -1   # -1 = none selected; 0..11 = waiting for click

# Spore canonical part catalog — name + DNA cost; used for the sidebar buttons.
# MUST match server net::PartKind enum order.
const PART_NAMES := [
	"Filter", "Jaw", "Proboscis",
	"Spike", "Poison", "Electric",
	"Flagella", "Cilia", "Jet",
	"Eye Beady", "Eye Stalk", "Eye Button",
]
const PART_COSTS := [15, 15, 25, 10, 15, 25, 15, 15, 25, 5, 5, 5]
const PART_COLORS := [
	Color(1.00, 1.00, 1.00),   # filter — white ring
	Color(1.00, 0.30, 0.20),   # jaw — red
	Color(1.00, 0.55, 0.85),   # proboscis — pink
	Color(1.00, 0.80, 0.20),   # spike — yellow
	Color(0.30, 0.95, 0.30),   # poison — green
	Color(0.40, 0.85, 1.00),   # electric — cyan
	Color(1.00, 0.40, 0.85),   # flagella — magenta tail
	Color(0.95, 0.95, 1.00),   # cilia — pale ring
	Color(0.30, 1.00, 0.95),   # jet — bright cyan
	Color(1.00, 1.00, 1.00),   # eye beady — white
	Color(1.00, 1.00, 0.95),   # eye stalk — pale yellow
	Color(0.95, 1.00, 1.00),   # eye button — pale cyan
]

# Most recent player DNA we know about. Used to gray out unaffordable
# part buttons in the sidebar.
var _player_dna: int = 0

# HUD — created in _ready, updated from update_player_stats() each frame
# the server pushes S_PLAYER_STATS (~15Hz). HP and DNA are the two values
# Spore Cell Stage ever shows; we follow the same convention.
var _hud_layer: CanvasLayer
var _hp_bar: ProgressBar
var _hp_label: Label
var _dna_label: Label

func _ready() -> void:
	_add_environment()
	_add_lights()
	_add_camera()
	_add_petri_dish()
	_add_dish_walls()
	_add_leviathans()
	_add_cells()
	_add_food()
	_add_decor()
	_add_hud()
	_add_parts_root()
	_add_editor_overlay()
	# No chevron — the player cell's big white cartoon eye + unique green
	# color already make heading and identity unmistakable.

func _process(dt: float) -> void:
	# Camera + chevron track the player every frame. We don't lerp because
	# input lag on a top-down view reads worse than instant jumps.
	if _camera != null:
		_camera.position = Vector3(
			_player_pos.x,
			CAMERA_HEIGHT,
			_player_pos.z + CAMERA_DISTANCE)
	if _chevron != null:
		_chevron.position = _player_pos + Vector3(0.0, 0.05, 0.0)
		_chevron.rotation = Vector3(0.0, -_player_angle, 0.0)
	# Parts root tracks player center and rotates with the player's heading
	# so attached parts stay glued to their cell-local position as the cell
	# turns underneath them.
	if _parts_root != null:
		_parts_root.position = _player_pos
		_parts_root.rotation = Vector3(0.0, -_player_angle, 0.0)

	# Bubbles drift on the XZ plane with a small per-bubble velocity; wrap
	# around dish bounds so a steady population is always visible.
	for i in _bubbles.size():
		var b: MeshInstance3D = _bubbles[i]
		var v: Vector2 = _bubble_drift[i]
		var p: Vector3 = b.position
		p.x += v.x * dt
		p.z += v.y * dt
		if p.x >  HALF_W: p.x = -HALF_W
		if p.x < -HALF_W: p.x =  HALF_W
		if p.z >  HALF_D: p.z = -HALF_D
		if p.z < -HALF_D: p.z =  HALF_D
		b.position = p

	# Crystals stay put but slowly rotate around Y so their facets catch
	# the light as the player swims past them.
	for i in _crystals.size():
		var c: MeshInstance3D = _crystals[i]
		c.rotate_y(_crystal_spin[i] * dt)

	# Leviathans drift very slowly across a huge area behind the play plane
	# and rotate around Y so their silhouette subtly morphs. Wrap area is
	# ~3× the dish so they're rarely all visible at once.
	var lev_half := HALF_W * 3.0
	for i in _leviathans.size():
		var lv: MeshInstance3D = _leviathans[i]
		var v: Vector2 = _leviathan_drift[i]
		var p: Vector3 = lv.position
		p.x += v.x * dt
		p.z += v.y * dt
		if p.x >  lev_half: p.x = -lev_half
		if p.x < -lev_half: p.x =  lev_half
		if p.z >  lev_half: p.z = -lev_half
		if p.z < -lev_half: p.z =  lev_half
		lv.position = p
		lv.rotate_y(_leviathan_spin[i] * dt)

# --- environment + lights ---------------------------------------------------

func _add_environment() -> void:
	# Spore Cell Stage "primordial pond" feel: deep teal-aqua background
	# (the off-dish water column), cool ambient, gentle bloom only on the
	# brightest highlights. Goal: saturated cells pop on a clearly underwater
	# field — never washed out white.
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.04, 0.18, 0.24)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.85, 0.95)
	env.ambient_light_energy = 0.40
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.tonemap_exposure = 0.95
	# Subtle glow only on emissive bits (cell membrane pulse, food motes).
	env.glow_enabled = true
	env.glow_intensity = 0.5
	env.glow_strength = 0.7
	env.glow_bloom = 0.05
	env.glow_hdr_threshold = 1.6
	# Faint aqua haze toward the back so the dish edge softens into water.
	env.fog_enabled = true
	env.fog_light_color = Color(0.10, 0.32, 0.42)
	env.fog_light_energy = 0.6
	env.fog_density = 0.012
	get_world_3d().environment = env

func _add_lights() -> void:
	# Cool overhead "sun through water" — slight blue tint, modest energy so
	# the petri shader's water gradient stays the dominant color in frame.
	var sun := DirectionalLight3D.new()
	sun.name = "DishLight"
	sun.light_color = Color(0.85, 0.95, 1.00)
	sun.light_energy = 0.85
	sun.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	sun.shadow_enabled = false
	add_child(sun)

func _add_camera() -> void:
	# Perspective + tilted down — pure top-down flattened cells into 2D
	# silhouettes. The Spore cell stage uses a slight perch angle so players
	# can read each cell's volume (dome bodies, raised eyes, wiggling tails).
	var cam := Camera3D.new()
	cam.name = "MainCamera"
	cam.position = Vector3(0.0, CAMERA_HEIGHT, CAMERA_DISTANCE)
	cam.rotation_degrees = Vector3(CAMERA_TILT_DEG, 0.0, 0.0)
	cam.projection = Camera3D.PROJECTION_PERSPECTIVE
	cam.fov = CAMERA_FOV_DEG
	cam.near = 0.1
	cam.far = 300.0
	cam.current = true
	add_child(cam)
	_camera = cam

# --- petri dish floor + rim wall --------------------------------------------

func _add_petri_dish() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(HALF_W * 2.0, HALF_D * 2.0)
	plane.subdivide_width = 0
	plane.subdivide_depth = 0

	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/petri.gdshader")

	var mi := MeshInstance3D.new()
	mi.name = "PetriFloor"
	mi.mesh = plane
	mi.material_override = mat
	mi.position = Vector3(0.0, -0.1, 0.0)
	add_child(mi)

func _add_dish_walls() -> void:
	# Glowing cyan rim around the dish so the play area is unmistakably bounded.
	var rim_mat := StandardMaterial3D.new()
	rim_mat.albedo_color = Color(1.00, 0.78, 0.45)
	rim_mat.emission_enabled = true
	rim_mat.emission = Color(1.00, 0.65, 0.30)
	rim_mat.emission_energy_multiplier = 1.4

	var ns := BoxMesh.new()                # north/south walls (along X)
	ns.size = Vector3(HALF_W * 2.0 + 0.6, 0.6, 0.3)
	for z in [-HALF_D - 0.15, HALF_D + 0.15]:
		var mi := MeshInstance3D.new()
		mi.mesh = ns
		mi.material_override = rim_mat
		mi.position = Vector3(0.0, 0.0, z)
		add_child(mi)

	var ew := BoxMesh.new()                # east/west walls (along Z)
	ew.size = Vector3(0.3, 0.6, HALF_D * 2.0 + 0.6)
	for x in [-HALF_W - 0.15, HALF_W + 0.15]:
		var mi := MeshInstance3D.new()
		mi.mesh = ew
		mi.material_override = rim_mat
		mi.position = Vector3(x, 0.0, 0.0)
		add_child(mi)

# --- background leviathans --------------------------------------------------
#
# Spore Cell Stage has enormous, barely-visible creatures drifting far behind
# the play area — the player is a speck inside a much bigger pond. We fake
# this with a handful of ellipsoid silhouettes placed far below the play plane
# (so the tilted camera sees them off in the murky depths) at scales much
# larger than the viewport, rendered unshaded with low alpha + fog tint so they
# read as distant clumps, not solid objects.
func _add_leviathans() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	# Muted teal/aqua palette — tint toward the background water color so
	# leviathans feel like they're *in* the murk, not painted on top of it.
	var tints := [
		Color(0.30, 0.55, 0.60),
		Color(0.25, 0.45, 0.55),
		Color(0.35, 0.60, 0.50),
		Color(0.20, 0.40, 0.50),
		Color(0.40, 0.55, 0.65),
	]
	for i in 6:
		# Use a SphereMesh squashed by scale — no need for full cell anatomy
		# at this distance; silhouette is all the player reads.
		var sm := SphereMesh.new()
		sm.radius = 1.0
		sm.height = 2.0
		sm.radial_segments = 20
		sm.rings = 10
		var mi := MeshInstance3D.new()
		mi.mesh = sm
		var mat := StandardMaterial3D.new()
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		var tint: Color = tints[i % tints.size()]
		tint.a = 0.55
		mat.albedo_color = tint
		# Emissive so leviathans survive through the teal fog and read as
		# glowing bioluminescent shapes rather than vanishing into the haze.
		mat.emission_enabled = true
		mat.emission = tint * 1.3
		mat.emission_energy_multiplier = 0.5
		# No depth write — leviathans must never occlude play-plane cells.
		mat.no_depth_test = false
		mat.disable_receive_shadows = true
		mi.material_override = mat
		# Place far beyond the dish (well outside the walls) and slightly above
		# the play plane so the tilted camera catches them against the teal
		# background water, not occluded by the dish floor. Each one is
		# 40–80 units across — much larger than the visible play area.
		var sx: float = rng.randf_range(40.0, 80.0)
		var sz: float = rng.randf_range(30.0, 60.0)
		var sy: float = rng.randf_range(12.0, 24.0)
		mi.scale = Vector3(sx, sy, sz)
		# Spread around the dish, biased toward the camera-forward direction
		# (-Z) so most leviathans appear on-screen behind the play area.
		var px: float = rng.randf_range(-120.0, 120.0)
		var pz: float = rng.randf_range(-170.0, -70.0)
		var py: float = rng.randf_range(3.0, 18.0)
		mi.position = Vector3(px, py, pz)
		mi.rotation = Vector3(0.0, rng.randf_range(0.0, TAU), 0.0)
		add_child(mi)
		_leviathans.append(mi)
		# Slow drift: ~1 unit/sec for something this big reads as barely moving.
		var speed := rng.randf_range(0.4, 1.2)
		var ang := rng.randf_range(0.0, TAU)
		_leviathan_drift.append(Vector2(cos(ang), sin(ang)) * speed)
		_leviathan_spin.append(rng.randf_range(-0.05, 0.05))

# --- microbe mesh builders --------------------------------------------------

# Full 3D ellipsoid body — symmetrical about every axis, parameterized by
# length (L, +X), width (W, +Z), height (H, +Y). Tessellation is a lat-lon
# grid. The mesh's origin is the ellipsoid center (not the bottom), so the
# body is a proper floating shape with volume below *and* above Y=0 rather
# than a pancake stuck to the floor.
#
# UV2.x = 0 flags body vertices for the microbe shader (breathe + wobble).
func _build_ellipsoid_body(length: float, width: float, height: float,
		segments: int, rings: int) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var grid: Array = []
	for r in range(rings + 1):
		var row: Array = []
		var lat: float = lerp(-PI * 0.5, PI * 0.5, float(r) / float(rings))
		var y: float = sin(lat) * height
		var rr: float = cos(lat)
		for s in range(segments + 1):
			var lon: float = float(s % segments) / float(segments) * TAU
			row.append(Vector3(cos(lon) * length * rr, y,
				sin(lon) * width * rr))
		grid.append(row)
	for r in range(rings):
		for s in range(segments):
			var a: Vector3 = grid[r][s]
			var b: Vector3 = grid[r][s + 1]
			var c: Vector3 = grid[r + 1][s + 1]
			var d: Vector3 = grid[r + 1][s]
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(c)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(b)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(d)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(c)
	st.generate_normals()
	return st.commit()

# Append a ring of radial cilia cones onto an existing SurfaceTool, positioned
# at the body's equator (y = 0). Each cilium is a triangular prism (tip +
# 3-vert base) so it has real thickness from every camera angle.
func _append_cilia(st: SurfaceTool, count: int, length_axis: float,
		width_axis: float, cilia_len: float) -> void:
	var thick := 0.06
	for i in range(count):
		var a: float = float(i) / float(count) * TAU
		var base: Vector3 = Vector3(
			cos(a) * length_axis, 0.0, sin(a) * width_axis)
		var dir: Vector3 = Vector3(cos(a), 0.0, sin(a)).normalized()
		var perp: Vector3 = Vector3(-dir.z, 0.0, dir.x)
		var tip: Vector3 = base + dir * cilia_len + Vector3(0.0, 0.02, 0.0)
		var b0: Vector3 = base + perp * thick + Vector3(0.0, -thick, 0.0)
		var b1: Vector3 = base - perp * thick + Vector3(0.0, -thick, 0.0)
		var b2: Vector3 = base + Vector3(0.0, thick * 1.5, 0.0)
		# 3 side faces from the triangular base up to the tip.
		var faces := [
			[b0, b1, tip],
			[b1, b2, tip],
			[b2, b0, tip],
		]
		for f in faces:
			st.set_uv2(Vector2(1.0, 0.0)); st.add_vertex(f[0])
			st.set_uv2(Vector2(1.0, 0.0)); st.add_vertex(f[1])
			st.set_uv2(Vector2(1.0, 1.0)); st.add_vertex(f[2])

# Append an inner nucleus ellipsoid — a smaller opaque organelle that shows
# through the translucent membrane. Sits slightly offset toward the rear so
# it reads as anatomy, not a dead-center dot.
func _append_nucleus(st: SurfaceTool, L: float, W: float, H: float) -> void:
	var nseg := 12
	var nring := 8
	var scale := 0.32
	var nL: float = L * scale
	var nW: float = W * scale
	var nH: float = H * scale
	var cx: float = -L * 0.18   # offset toward rear pole
	var cy: float = 0.0
	var grid: Array = []
	for r in range(nring + 1):
		var row: Array = []
		var lat: float = lerp(-PI * 0.5, PI * 0.5, float(r) / float(nring))
		var y: float = sin(lat) * nH + cy
		var rr: float = cos(lat)
		for s in range(nseg + 1):
			var lon: float = float(s % nseg) / float(nseg) * TAU
			row.append(Vector3(cos(lon) * nL * rr + cx, y, sin(lon) * nW * rr))
		grid.append(row)
	for r in range(nring):
		for s in range(nseg):
			var a: Vector3 = grid[r][s]
			var b: Vector3 = grid[r][s + 1]
			var c: Vector3 = grid[r + 1][s + 1]
			var d: Vector3 = grid[r + 1][s]
			# UV2.x = 3 → nucleus branch in shader; UV2.y encodes radial
			# distance from nucleus center for the shader's shade gradient.
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(c)
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(b)
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(d)
			st.set_uv2(Vector2(3.0, 0.0)); st.add_vertex(c)

# Append a tapered flagellum tail extruded from the body's rear pole (-X) as
# a triangular-prism strip that the shader will undulate.
func _append_flagellum(st: SurfaceTool, start_x: float, tail_len: float,
		base_w: float, base_h: float, tail_segs: int) -> void:
	for s in range(tail_segs):
		var t0: float = float(s) / float(tail_segs)
		var t1: float = float(s + 1) / float(tail_segs)
		var x0: float = lerp(start_x, start_x - tail_len, t0)
		var x1: float = lerp(start_x, start_x - tail_len, t1)
		var w0: float = base_w * (1.0 - t0 * 0.85)
		var w1: float = base_w * (1.0 - t1 * 0.85)
		var h0: float = base_h * (1.0 - t0 * 0.85)
		var h1: float = base_h * (1.0 - t1 * 0.85)
		var L0 := Vector3(x0, 0.0, -w0); var R0 := Vector3(x0, 0.0,  w0)
		var T0 := Vector3(x0, h0, 0.0)
		var L1 := Vector3(x1, 0.0, -w1); var R1 := Vector3(x1, 0.0,  w1)
		var T1 := Vector3(x1, h1, 0.0)
		var faces := [
			[L0, L1, T1, T0],
			[T0, T1, R1, R0],
			[R0, R1, L1, L0],
		]
		for f in faces:
			st.set_uv2(Vector2(2.0, t0)); st.add_vertex(f[0])
			st.set_uv2(Vector2(2.0, t1)); st.add_vertex(f[1])
			st.set_uv2(Vector2(2.0, t1)); st.add_vertex(f[2])
			st.set_uv2(Vector2(2.0, t0)); st.add_vertex(f[0])
			st.set_uv2(Vector2(2.0, t1)); st.add_vertex(f[2])
			st.set_uv2(Vector2(2.0, t0)); st.add_vertex(f[3])

# Build a full species mesh: symmetric ellipsoid body + species-specific
# appendages baked into the same ArrayMesh so the MultiMesh can draw it all
# in one batch. This returns the SAME result for every cell of a species —
# per-cell variation comes from the shader's per-instance seed, not geometry.
func _build_species_mesh(sp: int) -> ArrayMesh:
	var shape: Dictionary = SPECIES_SHAPE[sp]
	var L: float = shape["length"]
	var W: float = shape["width"]
	var H: float = shape["height"]
	var segs: int = shape["segments"]
	var rings: int = shape["rings"]

	# Start from the nucleus (opaque inner organelle — must draw first so the
	# depth buffer is populated before the translucent body writes to it),
	# then body, then appendages.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	_append_nucleus(st, L, W, H)
	var grid: Array = []
	for r in range(rings + 1):
		var row: Array = []
		var lat: float = lerp(-PI * 0.5, PI * 0.5, float(r) / float(rings))
		var y: float = sin(lat) * H
		var rr: float = cos(lat)
		for s in range(segs + 1):
			var lon: float = float(s % segs) / float(segs) * TAU
			row.append(Vector3(cos(lon) * L * rr, y, sin(lon) * W * rr))
		grid.append(row)
	for r in range(rings):
		for s in range(segs):
			var a: Vector3 = grid[r][s]
			var b: Vector3 = grid[r][s + 1]
			var c: Vector3 = grid[r + 1][s + 1]
			var d: Vector3 = grid[r + 1][s]
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(c)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(b)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(a)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(d)
			st.set_uv2(Vector2(0.0, 0.0)); st.add_vertex(c)

	match sp:
		1:
			# Ciliate — ring of 20 cilia at the body's equator.
			_append_cilia(st, 20, L, W, 0.45)
		2:
			# Flagellate — one trailing flagellum from the rear pole.
			_append_flagellum(st, -L, 2.6, 0.10, 0.12, 10)
		_:
			pass
	st.generate_normals()
	return st.commit()

# Per-species shader params. Saturated cartoon palette + per-species eye
# placement (eye_strength=0 disables the eye for species without a clear
# "front", e.g. radial ciliates).
const SPECIES_PARAMS := [
	# 0 amoeba — bright peachy orange, plump with a friendly front eye. The
	# nucleus is a soft warm dot (not dark) so the cell reads as cute, not
	# like a stained bacterium.
	{"cyto": Color(1.00, 0.75, 0.45), "mem": Color(1.00, 0.90, 0.65),
	 "nuc":  Color(1.00, 0.60, 0.30),
	 "eye_strength": 1.0, "eye_offset": Vector2(0.45, 0.05),
	 "eye_size": 0.26},
	# 1 ciliate — pastel sky blue; every cute cell deserves a face, so even the
	# radial ciliate now has a soft forward eye.
	{"cyto": Color(0.55, 0.85, 1.00), "mem": Color(0.80, 0.95, 1.00),
	 "nuc":  Color(0.45, 0.75, 1.00),
	 "eye_strength": 1.0, "eye_offset": Vector2(0.50, 0.0),
	 "eye_size": 0.22},
	# 2 flagellate — pastel bubblegum pink (not hot-dog red); nucleus is a
	# soft rose pink so it looks like a blushing cheek, not an organelle.
	{"cyto": Color(1.00, 0.70, 0.85), "mem": Color(1.00, 0.85, 0.93),
	 "nuc":  Color(1.00, 0.55, 0.75),
	 "eye_strength": 1.0, "eye_offset": Vector2(0.55, 0.05),
	 "eye_size": 0.24},
	# 3 player — bright cheerful mint green with the biggest eye of all.
	# Nucleus is a soft lime dot (not a dark stain) so the cell reads as a
	# happy little guy, not a lab specimen.
	{"cyto": Color(0.55, 1.00, 0.60), "mem": Color(0.80, 1.00, 0.85),
	 "nuc":  Color(0.50, 0.95, 0.40),
	 "eye_strength": 1.0, "eye_offset": Vector2(0.50, 0.0),
	 "eye_size": 0.32},
]

func _add_cells() -> void:
	_species_mm.clear()
	var meshes: Array = [
		_build_species_mesh(0),   # 0 amoeba
		_build_species_mesh(1),   # 1 ciliate
		_build_species_mesh(2),   # 2 flagellate
		_build_species_mesh(3),   # 3 player
	]
	for sp in range(NUM_SPECIES):
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.use_custom_data = true
		mm.mesh = meshes[sp]
		var cap: int = (1 if sp == PLAYER_SPECIES else CELL_CAPACITY)
		mm.instance_count = cap
		mm.visible_instance_count = 0

		var mat := ShaderMaterial.new()
		mat.shader = load("res://shaders/microbe.gdshader")
		var pal: Dictionary = SPECIES_PARAMS[sp]
		mat.set_shader_parameter("cyto_color",   pal["cyto"])
		mat.set_shader_parameter("mem_color",    pal["mem"])
		mat.set_shader_parameter("nuc_color",    pal["nuc"])
		mat.set_shader_parameter("eye_strength", pal["eye_strength"])
		mat.set_shader_parameter("eye_offset",   pal["eye_offset"])
		mat.set_shader_parameter("eye_size",     pal["eye_size"])

		var node := MultiMeshInstance3D.new()
		node.name = "Cells_sp%d" % sp
		node.multimesh = mm
		node.material_override = mat
		add_child(node)
		_species_mm.append(mm)

func _add_chevron() -> void:
	# Small white triangle floating just above the player cell — points along
	# the player's heading so the controlled blob is unmistakable.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_normal(Vector3(0.0, 1.0, 0.0))
	st.add_vertex(Vector3( 0.55, 0.0,  0.00))
	st.add_vertex(Vector3(-0.10, 0.0,  0.22))
	st.add_vertex(Vector3(-0.10, 0.0, -0.22))
	var mesh := st.commit()

	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(1.0, 1.0, 1.0)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 1.0, 0.95)
	mat.emission_energy_multiplier = 2.5
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED

	var mi := MeshInstance3D.new()
	mi.name = "PlayerChevron"
	mi.mesh = mesh
	mi.material_override = mat
	add_child(mi)
	_chevron = mi

# --- snapshot consumers (called from net_client.gd) -------------------------

func update_cells(records: Array) -> void:
	if _species_mm.size() != NUM_SPECIES:
		return
	var counts: Array = []
	counts.resize(NUM_SPECIES)
	counts.fill(0)
	for r in records:
		var sp: int = clamp(int(r.species), 0, NUM_SPECIES - 1)
		var mm: MultiMesh = _species_mm[sp]
		var cap: int = (1 if sp == PLAYER_SPECIES else CELL_CAPACITY)
		var slot: int = counts[sp]
		if slot >= cap:
			continue
		counts[sp] = slot + 1

		var id: int = r.id
		var seed: float
		if _cell_seeds.has(id):
			seed = _cell_seeds[id]
		else:
			seed = (float(id) * 1.618) + (id % 13) * 0.41
			_cell_seeds[id] = seed
		var size: float = float(r.size)
		if size <= 0.01:
			size = 0.5

		var t := Transform3D()
		# Negate angle: server uses atan2(z, x); rotating the basis around
		# +Y by -angle aligns local +X with the world heading vector.
		t.basis = Basis(Vector3(0.0, 1.0, 0.0), -float(r.angle)).scaled(
			Vector3(size, size, size))
		# Full-ellipsoid bodies extend below Y=0 (mesh origin = cell center).
		# Lift every cell so its bottom clears the dish floor and the whole
		# volume is visible from the tilted camera.
		var shape: Dictionary = SPECIES_SHAPE[sp]
		var body_h: float = float(shape["height"])
		var cell_y: float = body_h * size + CELL_Y_FLOAT
		t.origin = Vector3(r.x, cell_y, r.z)
		mm.set_instance_transform(slot, t)
		mm.set_instance_custom_data(slot,
			Color(seed, float(sp), size, 0.0))

		if sp == PLAYER_SPECIES:
			# _player_pos tracks the cell CENTER (including Y lift), so
			# camera follow, parts attachment, and editor click-to-place all
			# work in a single coordinate frame.
			_player_pos = t.origin
			_player_angle = float(r.angle)
			_player_size = size

	for sp in range(NUM_SPECIES):
		_species_mm[sp].visible_instance_count = counts[sp]

# --- food -------------------------------------------------------------------

func _add_food() -> void:
	# Each food kind has its own perfectly symmetric geometric mesh — real
	# micro-algae silhouettes, not gumballs:
	#   * PLANT → hexagonal bipyramid (diatom)
	#   * MEAT  → octahedron (crystalline cell debris)
	#   * EGG   → icosahedron (radiolarian/volvox)
	# Flat-shaded so each facet catches light distinctly.
	_food_mms.clear()
	var kind_specs := [
		{"radius": 0.26, "color": Color(0.40, 1.00, 0.35),
		 "emit":   Color(0.25, 0.95, 0.20), "energy": 1.2},  # plant
		{"radius": 0.32, "color": Color(1.00, 0.35, 0.25),
		 "emit":   Color(1.00, 0.15, 0.05), "energy": 1.5},  # meat
		{"radius": 0.36, "color": Color(1.00, 0.92, 0.88),
		 "emit":   Color(1.00, 0.85, 0.75), "energy": 1.1},  # egg
	]
	var mesh_builders := [
		_build_hex_bipyramid_mesh,   # plant
		_build_octahedron_mesh,      # meat
		_build_icosahedron_mesh,     # egg
	]
	for k in NUM_FOOD_KINDS:
		var radius: float = kind_specs[k]["radius"]
		var mesh: ArrayMesh = mesh_builders[k].call(radius)

		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = mesh
		mm.instance_count = FOOD_CAPACITY
		mm.visible_instance_count = 0

		var mat := StandardMaterial3D.new()
		mat.albedo_color = kind_specs[k]["color"]
		mat.emission_enabled = true
		mat.emission = kind_specs[k]["emit"]
		mat.emission_energy_multiplier = kind_specs[k]["energy"]
		mat.metallic = 0.15
		mat.roughness = 0.40

		var node := MultiMeshInstance3D.new()
		node.name = "Food_k%d" % k
		node.multimesh = mm
		node.material_override = mat
		add_child(node)
		_food_mms.append(mm)

# Hexagonal bipyramid — two 6-sided pyramids joined at their bases. Classic
# diatom silhouette: 12 perfectly symmetric triangular facets.
func _build_hex_bipyramid_mesh(radius: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var h: float = radius * 1.45
	var top := Vector3(0.0,  h, 0.0)
	var bot := Vector3(0.0, -h, 0.0)
	var ring: Array = []
	for i in 6:
		var a: float = float(i) / 6.0 * TAU
		ring.append(Vector3(cos(a) * radius, 0.0, sin(a) * radius))
	for i in 6:
		var p0: Vector3 = ring[i]
		var p1: Vector3 = ring[(i + 1) % 6]
		st.add_vertex(top); st.add_vertex(p0); st.add_vertex(p1)
		st.add_vertex(bot); st.add_vertex(p1); st.add_vertex(p0)
	st.generate_normals()
	return st.commit()

# Octahedron — 8 triangular faces, perfect 4-fold symmetry. Reads as a sharp
# crystalline mote of organic matter.
func _build_octahedron_mesh(radius: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var px := Vector3( radius, 0.0, 0.0); var nx := Vector3(-radius, 0.0, 0.0)
	var py := Vector3(0.0,  radius, 0.0); var ny := Vector3(0.0, -radius, 0.0)
	var pz := Vector3(0.0, 0.0,  radius); var nz := Vector3(0.0, 0.0, -radius)
	var faces := [
		[py, px, pz], [py, pz, nx], [py, nx, nz], [py, nz, px],
		[ny, pz, px], [ny, nx, pz], [ny, nz, nx], [ny, px, nz],
	]
	for f in faces:
		st.add_vertex(f[0]); st.add_vertex(f[1]); st.add_vertex(f[2])
	st.generate_normals()
	return st.commit()

# Icosahedron — 20 triangular faces, near-spherical but with clean facets.
# Reads as a radiolarian/volvox egg: rounder silhouette than octahedron, still
# unmistakably geometric.
func _build_icosahedron_mesh(radius: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var t: float = (1.0 + sqrt(5.0)) * 0.5   # golden ratio
	var verts := [
		Vector3(-1.0,  t, 0.0), Vector3( 1.0,  t, 0.0),
		Vector3(-1.0, -t, 0.0), Vector3( 1.0, -t, 0.0),
		Vector3(0.0, -1.0,  t), Vector3(0.0,  1.0,  t),
		Vector3(0.0, -1.0, -t), Vector3(0.0,  1.0, -t),
		Vector3( t, 0.0, -1.0), Vector3( t, 0.0,  1.0),
		Vector3(-t, 0.0, -1.0), Vector3(-t, 0.0,  1.0),
	]
	# Normalize to unit sphere, then scale to requested radius.
	for i in verts.size():
		verts[i] = verts[i].normalized() * radius
	var faces := [
		[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
		[1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
		[3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
		[4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
	]
	for f in faces:
		st.add_vertex(verts[f[0]])
		st.add_vertex(verts[f[1]])
		st.add_vertex(verts[f[2]])
	st.generate_normals()
	return st.commit()

# --- decor: bubbles + crystals ---------------------------------------------
#
# Both are real geometry in the scene tree (NOT shader fakes). Bubbles drift
# slowly across the dish; crystals sit and slowly rotate. Pure visual.

const NUM_BUBBLES  := 14
const NUM_CRYSTALS := 10

func _build_crystal_mesh() -> ArrayMesh:
	# A 6-point top-down star: 6 sharp triangles around a small hex core.
	# Looks like a stylized snow-crystal / ice spicule when seen from above.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var spikes := 6
	var inner := 0.18
	var outer := 0.55
	for i in range(spikes):
		var a0: float = float(i)            / float(spikes) * TAU
		var a1: float = float(i) + 0.5
		a1 = a1 / float(spikes) * TAU
		var a2: float = float(i + 1)        / float(spikes) * TAU
		var v_in0: Vector3 = Vector3(cos(a0) * inner, 0.04, sin(a0) * inner)
		var v_in1: Vector3 = Vector3(cos(a2) * inner, 0.04, sin(a2) * inner)
		var v_tip: Vector3 = Vector3(cos(a1) * outer, 0.04, sin(a1) * outer)
		st.set_normal(Vector3(0.0, 1.0, 0.0))
		st.add_vertex(v_in0)
		st.add_vertex(v_tip)
		st.add_vertex(v_in1)
		# Tiny center triangle for solid hex middle.
		st.add_vertex(Vector3.ZERO)
		st.add_vertex(v_in0)
		st.add_vertex(v_in1)
	return st.commit()

func _add_decor() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242

	# Bubbles — translucent spheres with a faint inner glow. SphereMesh +
	# StandardMaterial3D with transparency + emission rim approximates a
	# soap-bubble look without needing a custom shader.
	var bubble_mesh := SphereMesh.new()
	bubble_mesh.radial_segments = 14
	bubble_mesh.rings = 8
	bubble_mesh.radius = 0.45
	bubble_mesh.height = 0.90
	var bubble_mat := StandardMaterial3D.new()
	bubble_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	bubble_mat.albedo_color = Color(0.75, 0.95, 1.00, 0.30)
	bubble_mat.emission_enabled = true
	bubble_mat.emission = Color(0.55, 0.90, 1.00)
	bubble_mat.emission_energy_multiplier = 0.6
	bubble_mat.metallic = 0.6
	bubble_mat.roughness = 0.15
	bubble_mat.rim_enabled = true
	bubble_mat.rim = 0.75
	bubble_mat.rim_tint = 0.5
	for i in NUM_BUBBLES:
		var b := MeshInstance3D.new()
		b.name = "Bubble%d" % i
		b.mesh = bubble_mesh
		var s: float = rng.randf_range(0.55, 1.4)
		b.scale = Vector3(s, s, s)
		b.position = Vector3(
			rng.randf_range(-HALF_W + 2.0, HALF_W - 2.0),
			0.05,
			rng.randf_range(-HALF_D + 2.0, HALF_D - 2.0))
		b.material_override = bubble_mat
		add_child(b)
		_bubbles.append(b)
		# Each bubble drifts slowly in its own direction so the population
		# doesn't read as a parade — gentle currents, not a conveyor belt.
		var ang := rng.randf_range(-PI, PI)
		var spd := rng.randf_range(0.20, 0.55)
		_bubble_drift.append(Vector2(cos(ang) * spd, sin(ang) * spd))

	# Crystals — sharp 6-point stars in pale icy blue. Solid (not transparent)
	# so they read as physical objects. Slow rotation gives them life.
	var crystal_mesh := _build_crystal_mesh()
	var crystal_mat := StandardMaterial3D.new()
	crystal_mat.albedo_color = Color(0.78, 0.92, 1.00)
	crystal_mat.emission_enabled = true
	crystal_mat.emission = Color(0.55, 0.85, 1.00)
	crystal_mat.emission_energy_multiplier = 0.85
	crystal_mat.metallic = 0.4
	crystal_mat.roughness = 0.25
	crystal_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	for i in NUM_CRYSTALS:
		var c := MeshInstance3D.new()
		c.name = "Crystal%d" % i
		c.mesh = crystal_mesh
		var s: float = rng.randf_range(0.6, 1.3)
		c.scale = Vector3(s, s, s)
		c.position = Vector3(
			rng.randf_range(-HALF_W + 4.0, HALF_W - 4.0),
			0.06,
			rng.randf_range(-HALF_D + 4.0, HALF_D - 4.0))
		c.rotation.y = rng.randf_range(-PI, PI)
		c.material_override = crystal_mat
		add_child(c)
		_crystals.append(c)
		_crystal_spin.append(rng.randf_range(-0.30, 0.30))

func update_food(records: Array) -> void:
	if _food_mms.size() != NUM_FOOD_KINDS:
		return
	var counts: Array = []
	counts.resize(NUM_FOOD_KINDS)
	counts.fill(0)
	for r in records:
		var k: int = clamp(int(r.kind), 0, NUM_FOOD_KINDS - 1)
		var slot: int = counts[k]
		if slot >= FOOD_CAPACITY:
			continue
		counts[k] = slot + 1
		var t := Transform3D()
		t.origin = Vector3(r.x, 0.0, r.z)
		_food_mms[k].set_instance_transform(slot, t)
	for k in NUM_FOOD_KINDS:
		_food_mms[k].visible_instance_count = counts[k]

# --- HUD --------------------------------------------------------------------
#
# Minimum viable Spore HUD: a green HP bar at bottom-center and a DNA counter
# at the top-right. Both driven by S_PLAYER_STATS (15Hz). The server is the
# sole source of truth — no client-side prediction.

func _add_hud() -> void:
	_hud_layer = CanvasLayer.new()
	_hud_layer.name = "HUD"
	add_child(_hud_layer)

	# HP bar: green fill over a dark frame, centered at the bottom of the
	# screen. Anchors keep it in place as the window resizes.
	_hp_bar = ProgressBar.new()
	_hp_bar.name = "HpBar"
	_hp_bar.min_value = 0.0
	_hp_bar.max_value = 1.0
	_hp_bar.value = 1.0
	_hp_bar.show_percentage = false
	_hp_bar.custom_minimum_size = Vector2(320.0, 22.0)
	_hp_bar.anchor_left   = 0.5
	_hp_bar.anchor_right  = 0.5
	_hp_bar.anchor_top    = 1.0
	_hp_bar.anchor_bottom = 1.0
	_hp_bar.offset_left   = -160.0
	_hp_bar.offset_right  =  160.0
	_hp_bar.offset_top    = -48.0
	_hp_bar.offset_bottom = -26.0
	var bg := StyleBoxFlat.new()
	bg.bg_color = Color(0.06, 0.12, 0.14, 0.85)
	bg.border_color = Color(0.85, 0.95, 1.00, 0.90)
	bg.set_border_width_all(2)
	bg.set_corner_radius_all(4)
	var fg := StyleBoxFlat.new()
	fg.bg_color = Color(0.35, 0.95, 0.45)
	fg.set_corner_radius_all(3)
	_hp_bar.add_theme_stylebox_override("background", bg)
	_hp_bar.add_theme_stylebox_override("fill", fg)
	_hud_layer.add_child(_hp_bar)

	_hp_label = Label.new()
	_hp_label.name = "HpLabel"
	_hp_label.text = "HP 0 / 0"
	_hp_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_hp_label.anchor_left   = 0.5
	_hp_label.anchor_right  = 0.5
	_hp_label.anchor_top    = 1.0
	_hp_label.anchor_bottom = 1.0
	_hp_label.offset_left   = -160.0
	_hp_label.offset_right  =  160.0
	_hp_label.offset_top    = -74.0
	_hp_label.offset_bottom = -50.0
	_hp_label.add_theme_color_override("font_color", Color(1.0, 1.0, 1.0))
	_hp_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_hp_label.add_theme_constant_override("outline_size", 4)
	_hud_layer.add_child(_hp_label)

	# DNA counter: top-right, big readable number. In Spore this is the soft
	# currency that unlocks editor parts, so it's the first number the player
	# should learn to track.
	_dna_label = Label.new()
	_dna_label.name = "DnaLabel"
	_dna_label.text = "DNA 0"
	_dna_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_dna_label.anchor_left   = 1.0
	_dna_label.anchor_right  = 1.0
	_dna_label.anchor_top    = 0.0
	_dna_label.anchor_bottom = 0.0
	_dna_label.offset_left   = -220.0
	_dna_label.offset_right  = -16.0
	_dna_label.offset_top    = 12.0
	_dna_label.offset_bottom = 52.0
	_dna_label.add_theme_font_size_override("font_size", 26)
	_dna_label.add_theme_color_override("font_color", Color(0.85, 1.00, 0.70))
	_dna_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_dna_label.add_theme_constant_override("outline_size", 4)
	_hud_layer.add_child(_dna_label)

func update_player_stats(hp: float, max_hp: float, dna: int) -> void:
	if _hp_bar == null:
		return
	var mx: float = max(max_hp, 0.0001)
	_hp_bar.max_value = mx
	_hp_bar.value = clamp(hp, 0.0, mx)
	if _hp_label != null:
		_hp_label.text = "HP %d / %d" % [int(round(hp)), int(round(max_hp))]
	if _dna_label != null:
		_dna_label.text = "DNA %d" % dna
	# Re-color sidebar buttons whenever DNA changes — affordable parts go
	# white, unaffordable go gray.
	if dna != _player_dna:
		_player_dna = dna
		_refresh_editor_buttons()

# --- parts visualization -----------------------------------------------------
#
# Parts attach to a Node3D parent that follows the player. On each
# update_player_parts() snapshot we tear down + rebuild the whole tree —
# trivially cheap (≤24 instances) and avoids per-part diffing.

func _add_parts_root() -> void:
	_parts_root = Node3D.new()
	_parts_root.name = "PlayerParts"
	add_child(_parts_root)

func update_player_parts(records: Array) -> void:
	_parts_records = records
	if _parts_root == null:
		return
	for c in _parts_root.get_children():
		c.queue_free()
	for r in records:
		var k: int = int(r.kind)
		if k < 0 or k >= PART_NAMES.size():
			continue
		var node := _build_part_visual(k)
		_parts_root.add_child(node)
		_position_part_local(node, float(r.angle), float(r.distance))

func _build_part_visual(kind: int) -> Node3D:
	# Tiny stylized geometry per part. All parts are small (~0.3 units pre-
	# scale by player size) so they read as add-ons, not body replacements.
	var mat := StandardMaterial3D.new()
	mat.albedo_color = PART_COLORS[kind]
	mat.emission_enabled = true
	mat.emission = PART_COLORS[kind]
	mat.emission_energy_multiplier = 0.7

	var mi := MeshInstance3D.new()
	mi.material_override = mat
	match kind:
		0:  # Filter — flat translucent disc on the membrane
			var disc := CylinderMesh.new()
			disc.height = 0.05
			disc.top_radius = 0.30
			disc.bottom_radius = 0.30
			mi.mesh = disc
			mat.albedo_color = Color(1.0, 1.0, 1.0, 0.7)
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		1:  # Jaw — pair of red triangles (BoxMesh proxy)
			var box := BoxMesh.new()
			box.size = Vector3(0.45, 0.10, 0.18)
			mi.mesh = box
		2:  # Proboscis — narrow long cylinder
			var c := CylinderMesh.new()
			c.height = 0.55
			c.top_radius = 0.05
			c.bottom_radius = 0.10
			mi.mesh = c
			mi.rotation = Vector3(0, 0, PI * 0.5)  # lay along +X
		3:  # Spike — yellow cone
			var c := CylinderMesh.new()
			c.height = 0.55
			c.top_radius = 0.0
			c.bottom_radius = 0.10
			mi.mesh = c
			mi.rotation = Vector3(0, 0, PI * 0.5)
		4:  # Poison — small green sphere
			var s := SphereMesh.new()
			s.radius = 0.18
			s.height = 0.36
			mi.mesh = s
		5:  # Electric — bright cyan sphere with high emission
			var s := SphereMesh.new()
			s.radius = 0.18
			s.height = 0.36
			mi.mesh = s
			mat.emission_energy_multiplier = 2.0
		6:  # Flagella — long thin magenta tail
			var c := CylinderMesh.new()
			c.height = 0.85
			c.top_radius = 0.025
			c.bottom_radius = 0.05
			mi.mesh = c
			mi.rotation = Vector3(0, 0, PI * 0.5)
		7:  # Cilia — small pale ring (cylinder)
			var c := CylinderMesh.new()
			c.height = 0.35
			c.top_radius = 0.04
			c.bottom_radius = 0.04
			mi.mesh = c
			mi.rotation = Vector3(0, 0, PI * 0.5)
		8:  # Jet — cyan box, high emission
			var b := BoxMesh.new()
			b.size = Vector3(0.40, 0.18, 0.18)
			mi.mesh = b
			mat.emission_energy_multiplier = 1.4
		9, 10, 11:  # Eyes — small white spheres
			var s := SphereMesh.new()
			s.radius = 0.13
			s.height = 0.26
			mi.mesh = s
		_:
			var s := SphereMesh.new()
			s.radius = 0.15
			s.height = 0.30
			mi.mesh = s
	return mi

func _position_part_local(node: Node3D, angle: float, distance: float) -> void:
	# Parts attach on the SURFACE of the player's 3D ellipsoid body. Given
	# the editor's (angle, distance∈[0,1]) — radial fraction in the XZ
	# plane — we back-solve the latitude on the ellipsoid so the part sits
	# on the skin instead of burying inside it.
	#
	#   distance = 0 → ellipsoid apex (top pole, y = +H)
	#   distance = 1 → equator rim (widest circle, y = 0)
	var shape: Dictionary = SPECIES_SHAPE[PLAYER_SPECIES]
	var L: float = _player_size * float(shape["length"])
	var W: float = _player_size * float(shape["width"])
	var H: float = _player_size * float(shape["height"])
	# Latitude where cos(lat) = distance (equator=0 rad, top pole=π/2).
	var d_clamped: float = clamp(distance, 0.0, 0.999)
	var sin_lat: float = sqrt(max(0.0, 1.0 - d_clamped * d_clamped))
	var rx: float = L * d_clamped * cos(angle)
	var rz: float = W * d_clamped * sin(angle)
	var y_on_surf: float = H * sin_lat
	node.position = Vector3(rx, y_on_surf + 0.04, rz)
	node.rotation.y = -angle
	var part_scale: float = max(_player_size * 0.75, 0.6)
	node.scale = Vector3(part_scale, part_scale, part_scale)

# --- editor UI ---------------------------------------------------------------

func _add_editor_overlay() -> void:
	# CanvasLayer parented to the world; visible only while _editor_open.
	# Shows on the right side: a column of part buttons with cost labels,
	# a Reset button, and a status line with the currently selected part.
	_editor_layer = CanvasLayer.new()
	_editor_layer.name = "EditorHUD"
	_editor_layer.visible = false
	add_child(_editor_layer)

	_editor_panel = Panel.new()
	_editor_panel.name = "EditorPanel"
	_editor_panel.anchor_left   = 1.0
	_editor_panel.anchor_right  = 1.0
	_editor_panel.anchor_top    = 0.0
	_editor_panel.anchor_bottom = 1.0
	_editor_panel.offset_left   = -260.0
	_editor_panel.offset_right  = -10.0
	_editor_panel.offset_top    = 60.0
	_editor_panel.offset_bottom = -10.0
	var pbg := StyleBoxFlat.new()
	pbg.bg_color = Color(0.04, 0.10, 0.14, 0.90)
	pbg.border_color = Color(0.55, 0.85, 1.00, 0.85)
	pbg.set_border_width_all(2)
	pbg.set_corner_radius_all(8)
	_editor_panel.add_theme_stylebox_override("panel", pbg)
	_editor_layer.add_child(_editor_panel)

	var vbox := VBoxContainer.new()
	vbox.name = "PartList"
	vbox.anchor_left   = 0.0
	vbox.anchor_right  = 1.0
	vbox.anchor_top    = 0.0
	vbox.anchor_bottom = 1.0
	vbox.offset_left   = 8.0
	vbox.offset_right  = -8.0
	vbox.offset_top    = 8.0
	vbox.offset_bottom = -8.0
	vbox.add_theme_constant_override("separation", 4)
	_editor_panel.add_child(vbox)

	var title := Label.new()
	title.text = "CELL EDITOR"
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_color", Color(0.85, 1.0, 0.95))
	vbox.add_child(title)

	for k in PART_NAMES.size():
		var btn := Button.new()
		btn.name = "PartBtn_%d" % k
		btn.text = "%s   [%d DNA]" % [PART_NAMES[k], PART_COSTS[k]]
		btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
		btn.pressed.connect(_on_part_button_pressed.bind(k))
		vbox.add_child(btn)

	var sep := HSeparator.new()
	vbox.add_child(sep)

	var reset_btn := Button.new()
	reset_btn.text = "Reset (refund all)"
	reset_btn.pressed.connect(_on_reset_pressed)
	vbox.add_child(reset_btn)

	var help := Label.new()
	help.text = "Pick a part, then click on or near\nyour cell to attach.\n[C] toggle  [Esc] close"
	help.add_theme_color_override("font_color", Color(0.75, 0.85, 0.90))
	help.add_theme_font_size_override("font_size", 12)
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(help)
	_editor_help = help

	_editor_status = Label.new()
	_editor_status.text = "(no part selected)"
	_editor_status.add_theme_color_override("font_color", Color(1.0, 0.95, 0.55))
	_editor_status.add_theme_font_size_override("font_size", 13)
	vbox.add_child(_editor_status)

func _on_part_button_pressed(kind: int) -> void:
	if PART_COSTS[kind] > _player_dna:
		_editor_status.text = "Need %d DNA (have %d)" % [PART_COSTS[kind], _player_dna]
		return
	_placement_kind = kind
	_editor_status.text = "Placing: %s\nClick on cell..." % PART_NAMES[kind]

func _on_reset_pressed() -> void:
	var nc := _net_client()
	if nc and nc.has_method("send_reset_parts"):
		nc.send_reset_parts()
	_placement_kind = -1
	_editor_status.text = "(reset sent)"

func _net_client() -> Node:
	# LocalSim is a direct child of Main (this node) — see scenes/main.tscn.
	# The netcode lives there in a local-only shim; when we later reintroduce
	# a real TCP client we can rename or route via a common interface.
	return get_node_or_null("LocalSim")

func _refresh_editor_buttons() -> void:
	if _editor_panel == null:
		return
	var vbox := _editor_panel.get_node_or_null("PartList")
	if vbox == null:
		return
	for k in PART_NAMES.size():
		var btn := vbox.get_node_or_null("PartBtn_%d" % k)
		if btn == null: continue
		if PART_COSTS[k] <= _player_dna:
			btn.add_theme_color_override("font_color", Color(1, 1, 1))
		else:
			btn.add_theme_color_override("font_color", Color(0.45, 0.45, 0.5))

func _toggle_editor() -> void:
	_editor_open = not _editor_open
	if _editor_layer != null:
		_editor_layer.visible = _editor_open
	if not _editor_open:
		_placement_kind = -1

func force_open_editor() -> void:
	# Test/CI hook — exposes the C-key behavior to net_client when started
	# with --test-editor. Idempotent.
	if not _editor_open:
		_toggle_editor()

func _input(ev: InputEvent) -> void:
	if ev is InputEventKey and ev.pressed and not ev.echo:
		if ev.physical_keycode == KEY_C:
			_toggle_editor()
		elif ev.physical_keycode == KEY_ESCAPE and _editor_open:
			if _placement_kind >= 0:
				_placement_kind = -1
				_editor_status.text = "(no part selected)"
			else:
				_toggle_editor()
	elif ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT:
		if _editor_open and _placement_kind >= 0:
			_handle_place_click(ev.position)

func _handle_place_click(mouse_pos: Vector2) -> void:
	# Skip clicks inside the editor panel (so dragging on UI doesn't fire
	# placements). Panel rect is in viewport coords on the right side.
	if _editor_panel != null:
		var rect := _editor_panel.get_global_rect()
		if rect.has_point(mouse_pos):
			return
	if _camera == null:
		return
	# Project mouse to Y=0 plane in world space.
	var origin: Vector3 = _camera.project_ray_origin(mouse_pos)
	var normal: Vector3 = _camera.project_ray_normal(mouse_pos)
	if abs(normal.y) < 0.0001:
		return
	var t: float = -origin.y / normal.y
	var world: Vector3 = origin + normal * t
	var dx: float = world.x - _player_pos.x
	var dz: float = world.z - _player_pos.z
	var ang: float = atan2(dz, dx)
	# Clamp distance to [0,1] in cell-radius units. Past the membrane = pin
	# the part to the membrane edge.
	var radius: float = max(_player_size, 0.001)
	var dist: float = clamp(sqrt(dx * dx + dz * dz) / radius, 0.0, 1.0)
	var nc := _net_client()
	if nc and nc.has_method("send_buy_part"):
		nc.send_buy_part(_placement_kind, ang, dist)
	# Optimistic UX: deduct expected cost in the status line; the real DNA
	# update will arrive on the next S_PLAYER_STATS broadcast.
	_editor_status.text = "Placed %s\n(awaiting server)" % PART_NAMES[_placement_kind]
	_placement_kind = -1

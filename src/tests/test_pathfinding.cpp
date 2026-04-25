/**
 * test_pathfinding.cpp — Headless pathfinding regression tests.
 *
 * Runs from build/ directory so python/ and artifacts/ are accessible.
 *
 * Step 3 (current): harness smoke test only. Spawns a flat world + player,
 * ticks 60 frames, asserts the player exists and stays on the ground.
 * No pathfinding logic yet — this subtest proves the harness itself works.
 */

#include "server/test_server.h"
#include "server/world_template.h"
#include "server/world.h"
#include "python/python_bridge.h"
#include "agent/pathfind.h"
#include "client/path_executor.h"
#include "logic/entity.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/constants.h"
#include <glm/glm.hpp>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <algorithm>

namespace civcraft::test {

struct Result { std::string name; bool passed; std::string msg; };
static std::vector<Result> g_results;
static std::vector<std::shared_ptr<WorldTemplate>> g_templates;

static void initTemplates() {
	g_templates = {
		std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/pathfind_test.py"),
	};
}

// Dedicated pathfinding test arena (no village, no mobs, no chest).
static std::unique_ptr<TestServer> makeTestArena() {
	auto srv = std::make_unique<TestServer>(g_templates);
	WorldGenConfig wgc;
	wgc.mobs.clear();
	srv->createGame(42, 0, wgc);
	return srv;
}

// Place one solid block at (wx, wy, wz). Mirrors the pattern used by T17
// in test_e2e.cpp (see lines 2036-2043).
static void placeBlock(TestServer& srv, int wx, int wy, int wz, BlockId bid) {
	ChunkPos cp = World::worldToChunk(wx, wy, wz);
	Chunk* c = srv.chunks().getChunk(cp);
	if (!c) return;
	int lx = ((wx % 16) + 16) % 16;
	int ly = ((wy % 16) + 16) % 16;
	int lz = ((wz % 16) + 16) % 16;
	c->set(lx, ly, lz, bid);
}

// Place a 2-tall wall column at feet+head height so a 2-tall entity can't
// step through. wy = entity's feet-y.
static void placeWallColumn(TestServer& srv, int wx, int wy, int wz, BlockId bid) {
	placeBlock(srv, wx, wy,     wz, bid);
	placeBlock(srv, wx, wy + 1, wz, bid);
}

static void tickN(TestServer& srv, int frames) {
	constexpr float dt = 1.0f / 60.0f;
	for (int i = 0; i < frames; i++) srv.tick(dt);
}

static void run(const char* name, std::function<std::string()> fn) {
	printf("  %-55s", name);
	fflush(stdout);
	std::string err;
	try { err = fn(); }
	catch (std::exception& e) { err = std::string("EXCEPTION: ") + e.what(); }
	catch (...) { err = "UNKNOWN EXCEPTION"; }
	bool ok = err.empty();
	printf("%s\n", ok ? "PASS" : ("FAIL: " + err).c_str());
	g_results.push_back({name, ok, err});
}

// ── Path sampling + ASCII top-down chart ────────────────────────────────
//
// Samples positions across the ticking window, then prints a small
// X/Z top-down view: S=start, G=goal, F=final, .=sampled waypoint.
//
// Coordinates in chart: +X → right, +Z → down.
// Per-tick snapshot captured directly from the live Entity. These are the
// actual fields the server mutates inside updateNavigation() in
// src/platform/server/pathfind.h — proof of what the code did, not a model.
struct PathSample {
	int       tick;
	glm::vec3 pos;
	glm::vec3 vel;
	float     dodgeTimer;
	int       dodgeSign;
	bool      navActive;
	bool      onGround;
};

// ── PPM plot (top-down X/Z view, every tick) ───────────────────────────
// Writes a binary P6 PPM to /tmp/pathfind_<name>.ppm. One pixel = 1 sample.
// Much higher fidelity than the ASCII chart — shows the full 300-tick track.
static void writePlotPPM(const char* name,
                         const std::vector<PathSample>& samples,
                         glm::vec3 start, glm::vec3 goal, glm::vec3 final_,
                         const std::vector<glm::ivec3>& walls = {},
                         int scale = 20) {   // pixels per block
	if (samples.empty()) return;

	float minX = std::min({start.x, goal.x, final_.x});
	float maxX = std::max({start.x, goal.x, final_.x});
	float minZ = std::min({start.z, goal.z, final_.z});
	float maxZ = std::max({start.z, goal.z, final_.z});
	for (auto& s : samples) {
		minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
		minZ = std::min(minZ, s.pos.z); maxZ = std::max(maxZ, s.pos.z);
	}
	for (auto& w : walls) {
		minX = std::min(minX, (float)w.x); maxX = std::max(maxX, (float)(w.x + 1));
		minZ = std::min(minZ, (float)w.z); maxZ = std::max(maxZ, (float)(w.z + 1));
	}
	// Pad 2 blocks around.
	minX = std::floor(minX) - 2; maxX = std::ceil(maxX) + 2;
	minZ = std::floor(minZ) - 2; maxZ = std::ceil(maxZ) + 2;

	int W = (int)((maxX - minX) * scale);
	int H = (int)((maxZ - minZ) * scale);
	if (W < 16 || H < 16) return;
	std::vector<uint8_t> img(W * H * 3, 240);   // light gray floor

	auto px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		if (x < 0 || x >= W || y < 0 || y >= H) return;
		int i = (y * W + x) * 3;
		img[i] = r; img[i+1] = g; img[i+2] = b;
	};
	auto blob = [&](int cx, int cy, int rad, uint8_t r, uint8_t g, uint8_t b) {
		for (int dy = -rad; dy <= rad; dy++)
		for (int dx = -rad; dx <= rad; dx++)
			if (dx*dx + dy*dy <= rad*rad) px(cx+dx, cy+dy, r, g, b);
	};
	auto toPx = [&](glm::vec3 p) {
		int x = (int)((p.x - minX) * scale);
		int y = (int)((p.z - minZ) * scale);
		return std::pair<int,int>{x, y};
	};

	// 1-block grid lines (dark gray).
	for (int bx = (int)minX; bx <= (int)maxX; bx++) {
		int x = (int)((bx - minX) * scale);
		for (int y = 0; y < H; y++) px(x, y, 200, 200, 200);
	}
	for (int bz = (int)minZ; bz <= (int)maxZ; bz++) {
		int y = (int)((bz - minZ) * scale);
		for (int x = 0; x < W; x++) px(x, y, 200, 200, 200);
	}

	// Walls — dark charcoal squares, one block each.
	for (auto& w : walls) {
		int x0 = (int)((w.x - minX) * scale);
		int y0 = (int)((w.z - minZ) * scale);
		for (int dy = 0; dy < scale; dy++)
		for (int dx = 0; dx < scale; dx++)
			px(x0 + dx, y0 + dy, 60, 60, 60);
	}

	// Path — blue track, every tick.
	for (auto& s : samples) {
		auto [x, y] = toPx(s.pos);
		px(x, y,      40,  80, 220);
		px(x+1, y,    40,  80, 220);
		px(x, y+1,    40,  80, 220);
		px(x+1, y+1,  40,  80, 220);
	}

	// Markers.
	{ auto [x,y] = toPx(start);  blob(x, y, 5,   0, 180,   0); }  // S green
	{ auto [x,y] = toPx(goal);   blob(x, y, 5, 220,   0,   0); }  // G red
	{ auto [x,y] = toPx(final_); blob(x, y, 4,   0,   0, 200); }  // F blue

	char path[256];
	snprintf(path, sizeof(path), "/tmp/pathfind_%s.ppm", name);
	FILE* fp = fopen(path, "wb");
	if (!fp) { printf("    [plot] fopen %s failed\n", path); return; }
	fprintf(fp, "P6\n%d %d\n255\n", W, H);
	fwrite(img.data(), 1, img.size(), fp);
	fclose(fp);
	printf("    [plot] wrote %s  (%dx%d, %d samples, %.1f blocks x %.1f blocks)\n",
	       path, W, H, (int)samples.size(), maxX - minX, maxZ - minZ);
}

static void drawChart(const std::vector<PathSample>& samples,
                      glm::vec3 start, glm::vec3 goal, glm::vec3 final_) {
	if (samples.empty()) return;

	// Bounding box including start/goal/final.
	float minX = std::min({start.x, goal.x, final_.x});
	float maxX = std::max({start.x, goal.x, final_.x});
	float minZ = std::min({start.z, goal.z, final_.z});
	float maxZ = std::max({start.z, goal.z, final_.z});
	for (auto& s : samples) {
		minX = std::min(minX, s.pos.x); maxX = std::max(maxX, s.pos.x);
		minZ = std::min(minZ, s.pos.z); maxZ = std::max(maxZ, s.pos.z);
	}
	minX -= 1; maxX += 1; minZ -= 1; maxZ += 1;

	const int W = 60, H = 16;
	std::vector<std::string> grid(H, std::string(W, ' '));
	auto plot = [&](glm::vec3 p, char c) {
		int col = (int)((p.x - minX) / (maxX - minX) * (W - 1) + 0.5f);
		int row = (int)((p.z - minZ) / (maxZ - minZ) * (H - 1) + 0.5f);
		if (col < 0 || col >= W || row < 0 || row >= H) return;
		// Don't overwrite higher-priority markers.
		char& cell = grid[row][col];
		auto prio = [](char ch) {
			if (ch == 'S' || ch == 'G' || ch == 'F') return 3;
			if (ch == '.') return 2;
			return 0;
		};
		if (prio(c) >= prio(cell)) cell = c;
	};
	for (auto& s : samples) plot(s.pos, '.');
	plot(start,  'S');
	plot(goal,   'G');
	plot(final_, 'F');

	printf("\n    chart: S=start G=goal F=final .=path   "
	       "x:[%.1f..%.1f] z:[%.1f..%.1f]\n", minX, maxX, minZ, maxZ);
	printf("    +"); for (int i = 0; i < W; i++) printf("-"); printf("+\n");
	for (auto& row : grid) printf("    |%s|\n", row.c_str());
	printf("    +"); for (int i = 0; i < W; i++) printf("-"); printf("+\n");
}

// ── P01: harness smoke — player exists and stays above floor ────────────
static std::string p01_harness_smoke() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	if (pid == ENTITY_NONE) return "player not spawned";

	Entity* e = srv->getEntity(pid);
	if (!e) return "player entity missing from world";

	float y0 = e->position.y;
	tickN(*srv, 60);

	e = srv->getEntity(pid);
	if (!e) return "player despawned after 60 ticks";
	if (e->position.y < y0 - 5.0f) {
		char buf[128];
		snprintf(buf, sizeof(buf), "player fell through floor: y %.2f → %.2f", y0, e->position.y);
		return buf;
	}
	return "";
}

// P02/P03/P06/P09 deleted: they drove a live entity via `e->nav.setGoal()`,
// which was server-side greedy steering. Server nav has been removed —
// navigation is client-only (agent/pathfind.{h,cpp} + civcraft_engine.Navigator),
// and the end-to-end drive is exercised from the client smoke run instead.
#if 0
// ── P02: walk 10 blocks in a straight line on open ground ──────────────
static std::string p02_walk_straight_line_open() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	glm::vec3 start = e->position;
	glm::vec3 goal  = start + glm::vec3(10.0f, 0.0f, 0.0f);  // 10 blocks +X
	e->nav.setGoal(goal);

	// Sample EVERY tick — we want a real trace of what updateNavigation did.
	std::vector<PathSample> samples;
	const int totalFrames = 300;
	for (int f = 0; f < totalFrames; f++) {
		srv->tick(1.0f / 60.0f);
		Entity* ee = srv->getEntity(pid);
		if (!ee) break;
		samples.push_back({f, ee->position, ee->velocity,
			ee->nav.dodgeTimer, ee->nav.dodgeSign,
			ee->nav.active, ee->onGround});
	}

	e = srv->getEntity(pid);
	if (!e) return "player despawned";
	glm::vec3 final_ = e->position;

	// ── Numeric trace: print key transitions ────────────────────────────
	// Always print first few, last few, and any tick where dodge flipped
	// or nav cleared. This is the actual captured path from the server.
	printf("\n    trace (300 ticks, dt=1/60s, walk_speed from entity.def):\n");
	printf("    %-5s %-23s %-19s %-8s %s\n",
	       "tick", "position (x,y,z)", "velocity (vx,vz)", "dodge", "nav");
	auto printSample = [](const PathSample& s) {
		printf("    %5d (%6.2f,%5.2f,%6.2f) (%5.2f,%5.2f)   %c%d t=%.2f %s\n",
			s.tick, s.pos.x, s.pos.y, s.pos.z, s.vel.x, s.vel.z,
			s.dodgeSign > 0 ? '+' : (s.dodgeSign < 0 ? '-' : ' '),
			std::abs(s.dodgeSign), s.dodgeTimer,
			s.navActive ? "active" : "CLEARED");
	};
	int N = (int)samples.size();
	// First 3 ticks — shows greedy direction being set
	for (int i = 0; i < std::min(3, N); i++) printSample(samples[i]);
	if (N > 6) printf("    ...\n");
	// Any tick where nav cleared (arrival)
	for (int i = 1; i < N; i++) {
		if (samples[i-1].navActive && !samples[i].navActive) {
			printf("    --- nav.clear() fired (distXZ < navArriveDistance=1.2) ---\n");
			printSample(samples[i]);
			break;
		}
	}
	// Any dodge events
	int dodgeEvents = 0;
	for (int i = 1; i < N; i++) {
		if (samples[i-1].dodgeSign != samples[i].dodgeSign) {
			printSample(samples[i]);
			dodgeEvents++;
			if (dodgeEvents >= 3) break;
		}
	}
	if (dodgeEvents == 0) printf("    no dodge events — pure greedy straight-line\n");
	// Last 2 ticks
	if (N > 3) {
		printf("    ...\n");
		for (int i = std::max(0, N-2); i < N; i++) printSample(samples[i]);
	}

	drawChart(samples, start, goal, final_);
	writePlotPPM("p02", samples, start, goal, final_, {});

	glm::vec3 d = final_ - goal;
	float dist = std::sqrt(d.x*d.x + d.z*d.z);
	if (dist > 1.5f) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"did not arrive — final=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) dist=%.2f",
			final_.x, final_.y, final_.z, goal.x, goal.y, goal.z, dist);
		return buf;
	}
	return "";
}

// ── P03: S-maze (two offset walls forcing an S-curve) ──────────────────
// Layout, top-down (+X right, +Z down), player feet at y=9:
//
//      .  .  .  .  #  .  .  .  .  .  .  .  .       z=-3
//      .  .  .  .  #  .  .  .  .  .  .  .  .       z=-2
//      .  .  .  .  #  .  .  .  .  #  .  .  .       z=-1
//      S  .  .  .  #  .  .  .  .  #  .  .  G       z= 0   ← spawn/goal row
//      .  .  .  .  #  .  .  .  .  #  .  .  .       z=+1
//      .  .  .  .  .  .  .  .  .  #  .  .  .       z=+2
//      .  .  .  .  .  .  .  .  .  #  .  .  .       z=+3
//
// Wall A blocks x=4 at z∈[-3..+1] (gap at z≥+2).
// Wall B blocks x=9 at z∈[-1..+3] (gap at z≤-2).
// Only path around: head SE through gap at (4,+2), then NE through gap at (9,-2).
// Greedy steering can't plan this — it will wedge against wall A.
static std::string p03_s_maze() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	glm::vec3 start = e->position;
	int sx = (int)std::floor(start.x);
	int sy = (int)std::floor(start.y);
	int sz = (int)std::floor(start.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	std::vector<glm::ivec3> walls;
	// Wall A at x = sx+4, gap at z >= sz+2
	for (int dz = -3; dz <= 1; dz++) {
		placeWallColumn(*srv, sx + 4, sy, sz + dz, stone);
		walls.push_back({sx + 4, sy, sz + dz});
	}
	// Wall B at x = sx+9, gap at z <= sz-2
	for (int dz = -1; dz <= 3; dz++) {
		placeWallColumn(*srv, sx + 9, sy, sz + dz, stone);
		walls.push_back({sx + 9, sy, sz + dz});
	}

	glm::vec3 goal = start + glm::vec3(13.0f, 0.0f, 0.0f);
	e->nav.setGoal(goal);

	std::vector<PathSample> samples;
	const int totalFrames = 900;   // 15 seconds — give greedy plenty of rope
	for (int f = 0; f < totalFrames; f++) {
		srv->tick(1.0f / 60.0f);
		Entity* ee = srv->getEntity(pid);
		if (!ee) break;
		samples.push_back({f, ee->position, ee->velocity,
			ee->nav.dodgeTimer, ee->nav.dodgeSign,
			ee->nav.active, ee->onGround});
	}

	e = srv->getEntity(pid);
	if (!e) return "player despawned";
	glm::vec3 final_ = e->position;

	writePlotPPM("p03", samples, start, goal, final_, walls);

	glm::vec3 d = final_ - goal;
	float dist = std::sqrt(d.x*d.x + d.z*d.z);
	float xProgress = final_.x - start.x;
	printf("    result: xProgress=%.2f (need 13) distToGoal=%.2f walls=%zu samples=%zu\n",
	       xProgress, dist, walls.size(), samples.size());

	if (dist > 1.5f) {
		char buf[200];
		snprintf(buf, sizeof(buf),
			"did not arrive — final=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) dist=%.2f xProgress=%.2f",
			final_.x, final_.y, final_.z, goal.x, goal.y, goal.z, dist, xProgress);
		return buf;
	}
	return "";
}

#endif // end of disabled P02/P03 (server-nav drives)

// ── P04: stairs-up — planner unit test for the Jump primitive ──────────
// Builds a 3-step staircase at +X (each step one block higher than the
// previous) and calls GridPlanner::plan() directly. A correct A* must
// return a 3-waypoint path where every kind == Jump, no partial flag,
// and the final waypoint equals the goal cell.
//
// This test bypasses the server's greedy-steering nav entirely — it
// exercises the planner as a pure function over a WorldView.
struct ServerWorldView : WorldView {
	TestServer*          srv;
	const BlockRegistry* reg;
	explicit ServerWorldView(TestServer& s) : srv(&s), reg(&s.blockRegistry()) {}
	bool isSolid(glm::ivec3 p) const override {
		BlockId bid = srv->chunks().getBlock(p.x, p.y, p.z);
		return reg->get(bid).solid;
	}
};

static std::string p04_stairs_up_plan() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	// Entity feet cell: floor of position.
	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	// Staircase: each step is one stone block on top of the previous tier.
	//   step 1 floor at (sx+1, sy,   sz)  → feet land at sy+1
	//   step 2 floor at (sx+2, sy+1, sz)  → feet land at sy+2
	//   step 3 floor at (sx+3, sy+2, sz)  → feet land at sy+3
	placeBlock(*srv, sx + 1, sy,     sz, stone);
	placeBlock(*srv, sx + 2, sy,     sz, stone);
	placeBlock(*srv, sx + 2, sy + 1, sz, stone);
	placeBlock(*srv, sx + 3, sy,     sz, stone);
	placeBlock(*srv, sx + 3, sy + 1, sz, stone);
	placeBlock(*srv, sx + 3, sy + 2, sz, stone);

	glm::ivec3 startCell{sx,     sy,     sz};
	glm::ivec3 goalCell {sx + 3, sy + 3, sz};

	ServerWorldView view(*srv);
	GridPlanner     planner(view);
	Path            path = planner.plan(startCell, goalCell);

	printf("\n    plan: steps=%zu cost=%.2f partial=%s\n",
	       path.steps.size(), path.cost, path.partial ? "true" : "false");
	for (size_t i = 0; i < path.steps.size(); i++) {
		const char* k = path.steps[i].kind == MoveKind::Walk    ? "Walk"
		              : path.steps[i].kind == MoveKind::Jump    ? "Jump"
		              :                                           "Descend";
		auto p = path.steps[i].pos;
		printf("      [%zu] %-7s → (%d,%d,%d)\n", i, k, p.x, p.y, p.z);
	}

	if (path.partial)          return "plan returned partial";
	if (path.steps.empty())    return "plan returned empty path";
	if (path.steps.size() != 3) {
		char buf[128];
		snprintf(buf, sizeof(buf), "expected 3 Jumps, got %zu steps", path.steps.size());
		return buf;
	}
	for (size_t i = 0; i < path.steps.size(); i++) {
		if (path.steps[i].kind != MoveKind::Jump) {
			char buf[128];
			snprintf(buf, sizeof(buf), "step %zu is not Jump", i);
			return buf;
		}
	}
	const Waypoint& last = path.steps.back();
	if (last.pos != goalCell) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"final waypoint (%d,%d,%d) != goal (%d,%d,%d)",
			last.pos.x, last.pos.y, last.pos.z,
			goalCell.x, goalCell.y, goalCell.z);
		return buf;
	}
	return "";
}

// ── P05: bug-trap — robust planner gate (2D, canonical benchmark) ──────
// Concave pocket with the opening facing AWAY from the start. Any local
// steering (greedy + dodge) provably fails: distance-to-goal strictly
// increases on the only productive path. A* must detour south, around,
// and up through the opening.
//
// Top-down layout (+X right, +Z down), walls = 2-tall stone columns:
//   z\x     0  1  2  3  4  5
//   sz-2    .  .  #  #  #  .     top wall
//   sz-1    .  .  #  G  #  .     G inside pocket
//   sz      S  .  #  .  #  .     S outside, pocket sides
//   sz+1    .  .  .  .  .  .     ← opening (only way in/out)
//
// A* path: S → east → south → east → east → north into opening → north to G.
static std::string p05_bug_trap_plan() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	// Pocket walls (all at y = sy, sy+1 so a 2-tall entity can't step through).
	// Top edge (z = sz-2): x in {sx+2, sx+3, sx+4}
	placeWallColumn(*srv, sx + 2, sy, sz - 2, stone);
	placeWallColumn(*srv, sx + 3, sy, sz - 2, stone);
	placeWallColumn(*srv, sx + 4, sy, sz - 2, stone);
	// Side walls (z = sz-1 and sz): x in {sx+2, sx+4}
	placeWallColumn(*srv, sx + 2, sy, sz - 1, stone);
	placeWallColumn(*srv, sx + 4, sy, sz - 1, stone);
	placeWallColumn(*srv, sx + 2, sy, sz,     stone);
	placeWallColumn(*srv, sx + 4, sy, sz,     stone);
	// z = sz+1 is fully open — the pocket's only entrance.

	glm::ivec3 startCell{sx,     sy, sz};
	glm::ivec3 goalCell {sx + 3, sy, sz - 1};   // inside the pocket

	ServerWorldView view(*srv);
	GridPlanner::Config cfg;
	cfg.maxNodes = 8192;   // bigger budget than P04 — more cells to explore
	GridPlanner planner(view, cfg);
	Path path = planner.plan(startCell, goalCell);

	printf("\n    plan: steps=%zu cost=%.2f partial=%s\n",
	       path.steps.size(), path.cost, path.partial ? "true" : "false");
	for (size_t i = 0; i < path.steps.size(); i++) {
		const char* k = path.steps[i].kind == MoveKind::Walk    ? "Walk"
		              : path.steps[i].kind == MoveKind::Jump    ? "Jump"
		              :                                           "Descend";
		auto p = path.steps[i].pos;
		printf("      [%zu] %-7s → (%d,%d,%d)\n", i, k, p.x, p.y, p.z);
	}

	if (path.partial)       return "plan returned partial (pocket unreachable?)";
	if (path.steps.empty()) return "plan returned empty path";

	// Final waypoint must equal goal.
	const Waypoint& last = path.steps.back();
	if (last.pos != goalCell) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"final waypoint (%d,%d,%d) != goal (%d,%d,%d)",
			last.pos.x, last.pos.y, last.pos.z,
			goalCell.x, goalCell.y, goalCell.z);
		return buf;
	}

	// The path must go around — at some point it must have z >= sz+1
	// (south of the pocket row). This is the non-monotonic detour that
	// proves local steering can't solve it.
	bool detoured = false;
	for (auto& wp : path.steps) if (wp.pos.z >= sz + 1) { detoured = true; break; }
	if (!detoured) return "path did not detour south through the opening";

	// Every step must be Walk — this layout is flat, no jumps/descents needed.
	for (size_t i = 0; i < path.steps.size(); i++) {
		if (path.steps[i].kind != MoveKind::Walk) {
			char buf[128];
			snprintf(buf, sizeof(buf), "step %zu is not Walk (unexpected Jump/Descend)", i);
			return buf;
		}
	}
	return "";
}

#if 0
// ── P06: bug-trap integration — planner + executor drive live entity ──
// Same bug-trap layout as P05, but instead of asserting on the planned
// path, we actually drive the player entity through it:
//   plan(start, goal)  →  PathExecutor(path)
//   each tick: exec.tick() → Intent{Move, center}  →  e->nav.setGoal(target)
//   server's greedy steering handles the per-leg straight-line walk.
// Asserts the entity reaches within 1.5 blocks of the goal cell within
// a reasonable budget.
static std::string p06_bug_trap_drive() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	std::vector<glm::ivec3> walls;
	auto addWall = [&](int wx, int wz) {
		placeWallColumn(*srv, wx, sy, wz, stone);
		walls.push_back({wx, sy, wz});
	};
	// Same pocket as P05 (top + sides, opening to +z).
	addWall(sx + 2, sz - 2); addWall(sx + 3, sz - 2); addWall(sx + 4, sz - 2);
	addWall(sx + 2, sz - 1); addWall(sx + 4, sz - 1);
	addWall(sx + 2, sz    ); addWall(sx + 4, sz    );

	glm::ivec3 startCell{sx,     sy, sz};
	glm::ivec3 goalCell {sx + 3, sy, sz - 1};

	ServerWorldView view(*srv);
	GridPlanner     planner(view);
	Path            path = planner.plan(startCell, goalCell);
	if (path.partial || path.steps.empty()) return "planner failed to find path";

	PathExecutor exec;
	exec.setPath(path);

	glm::vec3 start = e->position;
	std::vector<PathSample> samples;
	const int totalFrames = 600;   // 10s at 60Hz
	int cursorChanges = 0;
	int lastCursor = -1;

	for (int f = 0; f < totalFrames; f++) {
		Entity* ee = srv->getEntity(pid);
		if (!ee) break;
		auto intent = exec.tick(ee->position, view);
		if (intent.kind == PathExecutor::Intent::Move) {
			ee->nav.setGoal(intent.target);
		}
		srv->tick(1.0f / 60.0f);
		ee = srv->getEntity(pid);
		if (!ee) break;
		samples.push_back({f, ee->position, ee->velocity,
			ee->nav.dodgeTimer, ee->nav.dodgeSign,
			ee->nav.active, ee->onGround});
		if (exec.done()) break;
		// Trace cursor advances so the log shows which waypoint is active.
		// (PathExecutor doesn't expose cursor — infer via intent target.)
		(void)lastCursor; (void)cursorChanges;
	}

	e = srv->getEntity(pid);
	if (!e) return "player despawned";
	glm::vec3 final_ = e->position;

	glm::vec3 goalCenter{goalCell.x + 0.5f, (float)goalCell.y, goalCell.z + 0.5f};
	writePlotPPM("p06", samples,
		start, goalCenter, final_, walls);

	float dx = final_.x - goalCenter.x;
	float dz = final_.z - goalCenter.z;
	float dist = std::sqrt(dx*dx + dz*dz);
	printf("    result: final=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f) dist=%.2f "
	       "done=%s samples=%zu\n",
	       final_.x, final_.y, final_.z,
	       goalCenter.x, goalCenter.y, goalCenter.z,
	       dist, exec.done() ? "true" : "false", samples.size());

	if (dist > 1.5f) {
		char buf[200];
		snprintf(buf, sizeof(buf),
			"did not reach pocket — final=(%.2f,%.2f,%.2f) dist=%.2f (budget=%d ticks)",
			final_.x, final_.y, final_.z, dist, totalFrames);
		return buf;
	}
	return "";
}

#endif // end of disabled P06 (server-nav drive)

// ── P07: planBatch — reverse-Dijkstra from one shared goal to N starts ─
// RTS group-command scenario: three units scattered at different cells,
// one right-clicked goal. Shared closed-set Dijkstra from the goal must
// hand back a path for each unit, all landing exactly on the goal, no
// partials, all-Walk (flat arena).
//
// Layout (top-down, +X right, +Z down):
//   z\x   -3  -2  -1   0   1   2   3
//   sz-2                    S2
//   sz                  G
//   sz+2  S0                     S1
//
// Starts chosen so no two are on the same row/column to stress the
// reconstruction walk (no chance of two starts accidentally sharing
// a tail).
static std::string p07_plan_batch_shared_goal() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	std::vector<glm::ivec3> starts = {
		{sx - 3, sy, sz + 2},
		{sx + 3, sy, sz + 2},
		{sx + 2, sy, sz - 2},
	};
	glm::ivec3 goal{sx, sy, sz};

	ServerWorldView view(*srv);
	GridPlanner     planner(view);
	std::vector<Path> paths = planner.planBatch(starts, goal);

	if (paths.size() != starts.size()) return "planBatch returned wrong size";

	printf("\n");
	for (size_t i = 0; i < paths.size(); i++) {
		printf("    start[%zu]=(%d,%d,%d): steps=%zu cost=%.2f partial=%s\n",
		       i, starts[i].x, starts[i].y, starts[i].z,
		       paths[i].steps.size(), paths[i].cost,
		       paths[i].partial ? "true" : "false");
	}
	for (size_t i = 0; i < paths.size(); i++) {
		if (paths[i].partial) {
			char buf[96]; snprintf(buf, sizeof(buf), "start %zu: partial", i);
			return buf;
		}
		if (paths[i].steps.empty()) {
			char buf[96]; snprintf(buf, sizeof(buf), "start %zu: empty path", i);
			return buf;
		}
		const Waypoint& last = paths[i].steps.back();
		if (last.pos != goal) {
			char buf[160];
			snprintf(buf, sizeof(buf),
				"start %zu: final wp (%d,%d,%d) != goal (%d,%d,%d)",
				i, last.pos.x, last.pos.y, last.pos.z, goal.x, goal.y, goal.z);
			return buf;
		}
		for (const auto& wp : paths[i].steps) {
			if (wp.kind != MoveKind::Walk) {
				char buf[96];
				snprintf(buf, sizeof(buf), "start %zu: non-Walk step on flat arena", i);
				return buf;
			}
		}
	}
	return "";
}

// ── P08: pathInvalidatedBy — corridor detection ────────────────────────
// Plan a straight-line path on open ground, then probe pathInvalidatedBy
// with block changes at varying Chebyshev distances from a mid-path
// waypoint. Changes inside cfg.corridorRadius must return true; a change
// far away must return false. Proves the invalidation gate is actually
// gating, not blanket-accepting.
static std::string p08_path_invalidation() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	// createGame only pre-generates a small region around spawn; walk a short
	// path that stays inside the loaded area rather than tick the server.
	// The goal here is to test pathInvalidatedBy(), not planner distance.
	ServerWorldView view(*srv);
	GridPlanner     planner(view);
	Path path = planner.plan({sx, sy, sz}, {sx + 3, sy, sz});
	if (path.partial || path.steps.empty()) return "planner failed on open ground";

	const int R = planner.config().corridorRadius;
	glm::ivec3 mid = path.steps[path.steps.size() / 2].pos;

	// On-path block — must invalidate.
	if (!planner.pathInvalidatedBy(path, mid)) {
		return "on-path block did not invalidate";
	}
	// Block exactly at Chebyshev = R from mid — must invalidate (≤ R).
	glm::ivec3 edge{mid.x + R, mid.y, mid.z};
	if (!planner.pathInvalidatedBy(path, edge)) {
		return "block at corridor edge did not invalidate";
	}
	// Block well outside the corridor (Chebyshev = R+5 from every step).
	glm::ivec3 farAway{mid.x, mid.y + R + 5, mid.z + R + 5};
	if (planner.pathInvalidatedBy(path, farAway)) {
		return "distant block falsely invalidated";
	}
	printf("\n    corridorRadius=%d mid=(%d,%d,%d) edge=(%d,%d,%d) far=(%d,%d,%d)\n",
		R, mid.x, mid.y, mid.z, edge.x, edge.y, edge.z,
		farAway.x, farAway.y, farAway.z);
	return "";
}

#if 0
// ── P09: plan+drive past a long wall (mirrors client-side RTS execution) ──
// Plans once with GridPlanner, then each tick lets PathExecutor feed the
// next waypoint into a short-range nav goal — the same shape the client's
// RtsExecutor uses, minus the TCP hop. A 9-block wall blocks any greedy
// detour; only waypoint-following can reach the goal.
static std::string p09_plan_drive_past_wall() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	// Clear a flat corridor so terrain noise doesn't fight the planner.
	BlockId air = BLOCK_AIR;
	for (int dx = -2; dx <= 8; dx++) {
		for (int dz = -4; dz <= 4; dz++) {
			placeBlock(*srv, sx + dx, sy,     sz + dz, air);
			placeBlock(*srv, sx + dx, sy + 1, sz + dz, air);
			placeBlock(*srv, sx + dx, sy - 1, sz + dz, stone);
		}
	}
	for (int dz = -1; dz <= 1; dz++) {
		placeWallColumn(*srv, sx + 3, sy, sz + dz, stone);
	}

	glm::ivec3 startCell{sx,     sy, sz};
	glm::ivec3 goalCell {sx + 6, sy, sz};

	ServerWorldView view(*srv);
	// Disable wall-clearance penalty in this test so baseline drive is
	// measured against straight-line geometry, not softened detours.
	GridPlanner::Config cfg;
	cfg.wallClearancePenalty = 0.0f;
	GridPlanner     planner(view, cfg);
	Path            path = planner.plan(startCell, goalCell);
	if (path.partial || path.steps.empty()) {
		char b[200];
		snprintf(b, sizeof(b),
			"planner failed — start=(%d,%d,%d) goal=(%d,%d,%d) partial=%d steps=%zu",
			startCell.x, startCell.y, startCell.z, goalCell.x, goalCell.y, goalCell.z,
			(int)path.partial, path.steps.size());
		return b;
	}

	PathExecutor exec;
	exec.setPath(path);

	const int totalFrames = 900;
	for (int f = 0; f < totalFrames; f++) {
		Entity* ee = srv->getEntity(pid);
		if (!ee) break;
		auto intent = exec.tick(ee->position, view);
		if (intent.kind == PathExecutor::Intent::Move) {
			ee->nav.setGoal(intent.target);
		}
		srv->tick(1.0f / 60.0f);
		if (exec.done()) break;
	}

	Entity* ee = srv->getEntity(pid);
	if (!ee) return "player despawned";
	glm::vec3 finalPos = ee->position;
	float cx = (float)goalCell.x + 0.5f;
	float cz = (float)goalCell.z + 0.5f;
	float dx = finalPos.x - cx;
	float dz = finalPos.z - cz;
	float dist = std::sqrt(dx*dx + dz*dz);

	printf("\n    final=(%.2f,%.2f,%.2f)  goal_center=(%.2f,%.2f)  dist=%.2f  wpSteps=%zu\n",
	       finalPos.x, finalPos.y, finalPos.z, cx, cz, dist, path.steps.size());

	if (dist > 2.0f) {
		char buf[200];
		snprintf(buf, sizeof(buf),
			"plan+drive failed — final=(%.2f,%.2f,%.2f) dist=%.2f",
			finalPos.x, finalPos.y, finalPos.z, dist);
		return buf;
	}
	return "";
}

#endif // end of disabled P09 (server-nav drive)

// P10 — a 1-wide tunnel "] [" with walls on both sides forces the planner
// through a narrow passage even with wallClearancePenalty > 0. Verifies
// clearance is a *preference*, not a hard constraint.
static std::string p10_narrow_tunnel_still_passes() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);
	if (stone == BLOCK_AIR) return "no stone in registry";

	// Flatten an open region; then build a vertical barrier at x=sx+3 spanning
	// z=sz-5..sz+5 with a single 1-wide gap at z=sz — that's the tunnel.
	for (int dx = -2; dx <= 8; dx++) {
		for (int dz = -6; dz <= 6; dz++) {
			placeBlock(*srv, sx + dx, sy,     sz + dz, BLOCK_AIR);
			placeBlock(*srv, sx + dx, sy + 1, sz + dz, BLOCK_AIR);
			placeBlock(*srv, sx + dx, sy - 1, sz + dz, stone);
		}
	}
	for (int dz = -5; dz <= 5; dz++) {
		if (dz == 0) continue;  // gap
		placeWallColumn(*srv, sx + 3, sy, sz + dz, stone);
	}

	glm::ivec3 startCell{sx,     sy, sz};
	glm::ivec3 goalCell {sx + 6, sy, sz};

	ServerWorldView view(*srv);
	// Use the production default (0.25) — this is the whole point of P10.
	GridPlanner     planner(view);
	Path            path = planner.plan(startCell, goalCell);
	if (path.partial || path.steps.empty()) {
		char b[200];
		snprintf(b, sizeof(b),
			"planner failed to thread narrow tunnel — partial=%d steps=%zu penalty=%.2f",
			(int)path.partial, path.steps.size(), planner.config().wallClearancePenalty);
		return b;
	}

	// The wall at x=sx+3 spans z=sz-5..sz+5 with exactly one gap at z=sz, and
	// start/goal sit on opposite sides — so `!path.partial` above is proof
	// the planner threaded the gap. Post-compression the gap cell itself is
	// dropped (collinear Walk), but the only standable crossing is at z=sz,
	// and the final waypoint must be the goal on the far side.
	const Waypoint& last = path.steps.back();
	if (last.pos != goalCell)
		return "path found, but did not terminate at goal (did not thread gap?)";

	printf("\n    tunnel path steps=%zu cost=%.2f (penalty=%.2f)\n",
	       path.steps.size(), path.cost, planner.config().wallClearancePenalty);
	return "";
}

// P11 — perf scaling. Measures how long planGroup takes at increasing unit
// counts (1, 10, 100, 1000) using both per-unit plan() (current code path)
// and planBatch() (shared reverse-Dijkstra). Reports ms and per-unit cost
// so we can see when RTS commands start to hitch.
static std::string p11_perf_scaling() {
	auto srv = makeTestArena();
	EntityId pid = srv->localPlayerId();
	Entity* e = srv->getEntity(pid);
	if (!e) return "player missing";

	int sx = (int)std::floor(e->position.x);
	int sy = (int)std::floor(e->position.y);
	int sz = (int)std::floor(e->position.z);

	BlockId stone = srv->blockRegistry().getId(BlockType::Stone);

	// Flatten a 40×40 plaza so terrain doesn't dominate timing.
	for (int dx = -20; dx <= 20; dx++) {
		for (int dz = -20; dz <= 20; dz++) {
			placeBlock(*srv, sx + dx, sy,     sz + dz, BLOCK_AIR);
			placeBlock(*srv, sx + dx, sy + 1, sz + dz, BLOCK_AIR);
			placeBlock(*srv, sx + dx, sy - 1, sz + dz, stone);
		}
	}

	ServerWorldView view(*srv);

	auto timeMs = [](auto fn) {
		auto t0 = std::chrono::steady_clock::now();
		fn();
		auto t1 = std::chrono::steady_clock::now();
		return std::chrono::duration<double, std::milli>(t1 - t0).count();
	};

	printf("\n    N        plan() ms   planBatch ms   planFlow ms   flow/unit us\n");
	for (int n : {1, 10, 100, 1000}) {
		std::vector<glm::ivec3> starts;
		starts.reserve(n);
		for (int i = 0; i < n; i++) {
			int r = 3 + (i / 40);
			int a = i % 40;
			int dx = (int)(std::cos(a * 0.157) * r);
			int dz = (int)(std::sin(a * 0.157) * r);
			starts.push_back({sx + dx - 15, sy, sz + dz});
		}
		glm::ivec3 goal{sx + 15, sy, sz};

		double msPer = timeMs([&]{
			GridPlanner planner(view);
			for (auto& s : starts) (void)planner.plan(s, goal);
		});
		double msBatch = timeMs([&]{
			GridPlanner planner(view);
			(void)planner.planBatch(starts, goal);
		});
		double msFlow = timeMs([&]{
			GridPlanner planner(view);
			(void)planner.planFlowField(goal, starts);
		});

		printf("    %-8d %10.2f   %12.2f   %11.2f   %12.1f\n",
		       n, msPer, msBatch, msFlow, msFlow * 1000.0 / n);
	}
	return "";
}

// ── P12 — PathExecutor must not flip moveTarget 180° between ticks ─────
//
// The invariant: for any sensible path, driving an entity via
// `vel = normalize(PathExecutor::tick(...).target - pos) * walk_speed`
// must never produce a direction vector that reverses more than 120°
// from one agent tick to the next. A 180° reversal is the "spin in
// place" symptom observed in /tmp/civcraft_entity_2407.log — the
// entity gets pushed back and forth across a waypoint center it's
// already inside.
//
// We don't hard-code any single path. Instead, we run the same invariant
// against a family of synthetic shapes (straight / L / staircase of
// turns / real recorded plan), using realistic entity coasting between
// agent ticks. The coast is what exposes the bug: with no overshoot
// an entity never crosses a cell center, so cursor advance is trivial.
struct AirWorld : WorldView {
	bool isSolid(glm::ivec3 p) const override { return p.y < 0; }
};

// Run one scenario: drive PathExecutor over `path` for at most maxTicks
// agent ticks, coasting at walk_speed between decisions. Reports (tick,
// prevRow, flipRow, worstDot). If no flip occurs, firstFlipT == -1.
struct FlipReport {
	int       firstFlipT = -1;
	float     worstDot   = 2.0f;
	int       totalTicks = 0;
	glm::vec3 prevPos{}, prevTarget{};
	glm::vec2 prevDir{};
	glm::vec3 flipPos{}, flipTarget{};
	glm::vec2 flipDir{};
};

static FlipReport runScenario(const Path& path, glm::vec3 startPos,
                              int physPerAgent = 2, int maxTicks = 2000) {
	FlipReport rep;
	AirWorld world;
	(void)world;  // No-op for AirWorld — unified PathExecutor doesn't consult it.
	constexpr EntityId kTestEid = 42;
	PathExecutor exec;
	exec.setPath(kTestEid, path);

	constexpr float speed  = 2.5f;
	constexpr float physDt = 1.0f / 60.0f;
	const float     agentDt = physDt * physPerAgent;

	// Build a y-for-x lookup from the path so coasting keeps entity Y
	// near waypoint Y (the kArriveY=1.0 guard is real physics, not the
	// bug under test — a falling entity will naturally line up with the
	// floor). Use the nearest same-plane waypoint's Y.
	auto nearestY = [&](glm::vec3 p) {
		int bestIdx = 0;
		float bestD2 = 1e30f;
		for (size_t i = 0; i < path.steps.size(); ++i) {
			float dx = p.x - (path.steps[i].pos.x + 0.5f);
			float dz = p.z - (path.steps[i].pos.z + 0.5f);
			float d2 = dx*dx + dz*dz;
			if (d2 < bestD2) { bestD2 = d2; bestIdx = (int)i; }
		}
		return (float)path.steps[bestIdx].pos.y;
	};

	glm::vec3 pos = startPos;
	glm::vec3 vel{0, 0, 0};
	glm::vec2 prevDir{0, 0};
	glm::vec3 prevPos{}, prevTarget{};
	bool havePrev = false;

	// Pop-front model — no cursor state; record the remaining waypoint count
	// instead so the flip dump still shows which waypoint the executor was
	// driving toward.
	struct TickLog {
		int t = -1;
		int remaining = -1;
		glm::vec3 pos{}, target{};
		glm::vec2 dir{};
		bool nearWaypoint = false;
	};
	constexpr int kWindow = 8;
	TickLog window[kWindow];
	int winHead = 0;
	auto pushLog = [&](const TickLog& e) {
		window[winHead] = e;
		winHead = (winHead + 1) % kWindow;
	};
	bool dumped = false;
	auto dumpWindow = [&](int flipT) {
		if (dumped) return; dumped = true;
		printf("\n      ── tick trace (last %d ticks before flip@%d) ──\n",
		       kWindow, flipT);
		for (int k = 0; k < kWindow; ++k) {
			const TickLog& e = window[(winHead + k) % kWindow];
			if (e.t < 0) continue;
			printf("      t=%4d rem=%3d pos=(%7.3f,%7.3f) tgt=(%6.2f,%6.2f)"
			       " dir=(%+6.3f,%+6.3f)%s\n",
			       e.t, e.remaining,
			       e.pos.x, e.pos.z,
			       e.target.x, e.target.z,
			       e.dir.x, e.dir.y,
			       e.nearWaypoint ? "  near" : "");
		}
		printf("      remaining waypoints:");
		int dumpN = std::min((int)exec.path(kTestEid).steps.size(), 8);
		for (int i = 0; i < dumpN; ++i) {
			const auto& w = exec.path(kTestEid).steps[i];
			printf(" %d:(%d,%d,%d)%s", i, w.pos.x, w.pos.y, w.pos.z,
			       i == 0 ? "*" : "");
		}
		printf("\n");
	};

	for (int t = 0; t < maxTicks && !exec.done(kTestEid); ++t) {
		pos.x += vel.x * agentDt;
		pos.z += vel.z * agentDt;
		pos.y  = nearestY(pos);

		auto intent = exec.tick(kTestEid, pos);
		auto mkLog = [&](int tt, glm::vec3 p, glm::vec3 tg, glm::vec2 d, bool near) {
			return TickLog{tt, (int)exec.path(kTestEid).steps.size(), p, tg, d, near};
		};
		if (intent.kind != PathExecutor::Intent::Move) {
			vel = glm::vec3(0);
			continue;
		}
		glm::vec2 delta{intent.target.x - pos.x, intent.target.z - pos.z};
		float len = std::sqrt(delta.x*delta.x + delta.y*delta.y);
		if (len < 0.01f) {
			// `nav-near-waypoint` tick in agent.h — velocity zeroed for
			// this frame. Don't reset havePrev: a flip can straddle it.
			vel = glm::vec3(0);
			pushLog(mkLog(t, pos, intent.target, {0,0}, true));
			continue;
		}
		glm::vec2 dir = delta / len;

		if (havePrev) {
			float dot = prevDir.x*dir.x + prevDir.y*dir.y;
			if (dot < rep.worstDot) rep.worstDot = dot;
			if (dot < -0.5f && rep.firstFlipT < 0) {
				rep.firstFlipT = t;
				rep.prevPos    = prevPos;
				rep.prevTarget = prevTarget;
				rep.prevDir    = prevDir;
				rep.flipPos    = pos;
				rep.flipTarget = intent.target;
				rep.flipDir    = dir;
				pushLog(mkLog(t, pos, intent.target, dir, false));
				dumpWindow(t);
			}
		}
		if (!dumped) pushLog(mkLog(t, pos, intent.target, dir, false));
		prevDir    = dir;
		prevPos    = pos;
		prevTarget = intent.target;
		havePrev   = true;

		vel.x = dir.x * speed;
		vel.z = dir.y * speed;
		rep.totalTicks = t + 1;
	}
	return rep;
}

static Path makeStraightPath(int fromX, int toX, int y, int z) {
	Path p;
	int step = fromX < toX ? 1 : -1;
	for (int x = fromX; x != toX + step; x += step)
		p.steps.push_back({{x, y, z}, MoveKind::Walk});
	return p;
}

static Path makeLPath() {
	// Walk west then north — turn at the elbow is where overshoot is likely.
	Path p;
	for (int x = 10; x >= 0;  --x) p.steps.push_back({{x, 0, 0}, MoveKind::Walk});
	for (int z = -1; z >= -10; --z) p.steps.push_back({{0, 0, z}, MoveKind::Walk});
	return p;
}

static Path makeZigzagPath() {
	// Alternating 2-cell N-W segments — forces look-ahead to break at
	// every other waypoint.
	Path p;
	int x = 10, z = 0;
	for (int i = 0; i < 10; ++i) {
		p.steps.push_back({{x,   0, z}, MoveKind::Walk});
		p.steps.push_back({{x-1, 0, z}, MoveKind::Walk});
		x -= 1;
		p.steps.push_back({{x, 0, z-1}, MoveKind::Walk});
		z -= 1;
	}
	return p;
}

// Exact 86-waypoint plan from /tmp/civcraft_entity_2407.log's heartbeat.
static Path makeRecordedPath() {
	const glm::ivec3 wps[] = {
		{13,1,-42},{12,0,-42},{11,0,-42},{11,0,-41},{10,0,-41},{9,0,-41},
		{9,0,-40},{9,0,-39},{9,0,-38},{8,0,-38},{8,0,-37},{7,0,-37},
		{6,0,-37},{5,0,-37},{4,0,-37},{3,0,-37},{2,0,-37},{1,0,-37},
		{0,0,-37},{-1,0,-37},{-2,0,-37},{-3,0,-37},{-3,0,-36},{-4,0,-36},
		{-5,0,-36},{-6,0,-36},{-7,0,-36},{-8,0,-36},{-9,0,-36},{-10,0,-36},
		{-11,0,-36},{-12,0,-36},{-13,0,-36},{-13,0,-35},{-14,0,-35},
		{-14,0,-34},{-15,0,-34},{-15,0,-33},{-16,0,-33},{-17,0,-33},
		{-17,0,-32},{-18,0,-32},{-18,0,-31},{-18,0,-30},{-19,0,-30},
		{-19,0,-29},{-20,0,-29},{-21,0,-29},{-22,0,-29},{-22,0,-28},
		{-23,0,-28},{-23,0,-27},{-23,0,-26},{-23,0,-25},{-23,0,-24},
		{-23,0,-23},{-23,0,-22},{-23,0,-21},{-23,0,-20},{-23,0,-19},
		{-24,0,-19},{-25,0,-19},{-26,0,-19},{-27,0,-19},{-28,0,-19},
		{-29,0,-19},{-29,0,-18},{-29,0,-17},{-29,0,-16},{-29,0,-15},
		{-30,0,-15},{-31,1,-15},{-32,1,-15},{-33,1,-15},{-34,1,-15},
		{-35,1,-15},{-36,1,-15},{-37,1,-15},{-38,1,-15},{-39,1,-15},
		{-40,1,-15},{-41,1,-15},{-42,1,-15},{-42,1,-14},{-42,1,-13},
		{-42,1,-12},
	};
	Path p;
	for (size_t i = 0; i < sizeof(wps)/sizeof(wps[0]); ++i) {
		MoveKind k = MoveKind::Walk;
		if (i > 0 && wps[i].y < wps[i-1].y)      k = MoveKind::Descend;
		else if (i > 0 && wps[i].y > wps[i-1].y) k = MoveKind::Jump;
		p.steps.push_back({wps[i], k});
	}
	return p;
}

static std::string p12_no_opposite_movetargets() {
	struct Case { const char* name; Path path; glm::vec3 start; };
	std::vector<Case> cases = {
		{"straight-west",   makeStraightPath(10, -10, 0,  0), {10.5f, 0.0f,  0.5f}},
		{"straight-east",   makeStraightPath(-10, 10, 0,  5), {-9.5f, 0.0f,  5.5f}},
		{"L-turn",          makeLPath(),                      {10.5f, 0.0f,  0.5f}},
		{"zigzag",          makeZigzagPath(),                 {10.5f, 0.0f,  0.5f}},
		{"recorded-2407",   makeRecordedPath(),               {14.07f, 1.01f, -41.43f}},
	};

	int failed = 0;
	std::string firstError;
	for (auto& c : cases) {
		FlipReport r = runScenario(c.path, c.start);
		printf("\n    %-16s ticks=%d worstDot=%+.3f",
		       c.name, r.totalTicks, r.worstDot);
		if (r.firstFlipT >= 0) {
			printf("  FLIP@%d\n", r.firstFlipT);
			printf("      prev: pos=(%7.3f,%7.3f) tgt=(%6.2f,%6.2f) "
			       "dir=(%+6.3f,%+6.3f)\n",
			       r.prevPos.x, r.prevPos.z,
			       r.prevTarget.x, r.prevTarget.z,
			       r.prevDir.x, r.prevDir.y);
			printf("      flip: pos=(%7.3f,%7.3f) tgt=(%6.2f,%6.2f) "
			       "dir=(%+6.3f,%+6.3f)\n",
			       r.flipPos.x, r.flipPos.z,
			       r.flipTarget.x, r.flipTarget.z,
			       r.flipDir.x, r.flipDir.y);
			if (firstError.empty()) {
				char buf[300];
				snprintf(buf, sizeof(buf),
					"[%s] direction flipped >120° at tick %d "
					"(worstDot=%.3f)",
					c.name, r.firstFlipT, r.worstDot);
				firstError = buf;
			}
			failed++;
		} else {
			printf("  OK\n");
		}
	}
	printf("\n    %d/%zu scenarios OK\n",
	       (int)cases.size() - failed, cases.size());
	return firstError;
}

// ── P13 — Half-plane retire: teleport past the front must not stall ────
// Drives entity to (0.5), then teleports forward to (4.5) in a single tick
// — the front three waypoints were skipped without the entity ever entering
// their arrive ring. The executor must recognize that those cells are now
// "behind" the entity (projected along segment direction) and pop them,
// otherwise the next Move target points backward.
//
// This covers collision push-out, clientPos snap-back, and any out-of-band
// shove that can leave the entity outside every remaining cell's arrive
// disk. The scenario intentionally picks a jump size (4 cells) bigger than
// the arrive radius so a single-cell pop can't hide the bug.
static std::string p13_teleport_past_front() {
	constexpr EntityId kEid = 77;
	Path p;
	for (int x = 0; x <= 10; ++x) p.steps.push_back({{x, 0, 0}, MoveKind::Walk});

	PathExecutor exec;
	exec.setPath(kEid, p);

	// First tick anchors at the start cell.
	glm::vec3 pos{0.5f, 0, 0.5f};
	auto intent = exec.tick(kEid, pos);
	if (intent.kind != PathExecutor::Intent::Move)
		return "tick 0: expected Move intent, got None";

	// Teleport: entity jumps 4 cells forward in a single tick. Cells 1..3
	// were skipped — the entity was never inside their arrive ring.
	pos = glm::vec3{4.5f, 0, 0.5f};
	intent = exec.tick(kEid, pos);
	if (intent.kind != PathExecutor::Intent::Move)
		return "tick 1: expected Move intent, got None";

	// Direction from entity to target must point +x (forward along path). A
	// -x result means the front is still cell 1 (3 blocks behind), which is
	// exactly the stall case the half-plane retire is meant to catch.
	float dx = intent.target.x - pos.x;
	printf("    after tp(+4): pos=%.2f tgt=%.2f dx=%+.2f remaining=%zu\n",
	       pos.x, intent.target.x, dx, exec.path(kEid).steps.size());
	if (dx < 0.0f) {
		char b[200];
		snprintf(b, sizeof(b),
			"front cell is behind entity (tgt=%.2f, pos=%.2f, dx=%+.2f) — "
			"half-plane retire failed", intent.target.x, pos.x, dx);
		return b;
	}
	// Path should have shrunk: cells 0..4 (entity sits on 4) pop, so at most
	// 6 remain.
	size_t rem = exec.path(kEid).steps.size();
	if (rem > 6) {
		char b[160];
		snprintf(b, sizeof(b),
			"path did not shrink enough after teleport: remaining=%zu (want ≤ 6)",
			rem);
		return b;
	}
	return "";
}

// ── P14 — Closed door off-path (regression) ───────────────────────────────
// Collinear-Walk compression drops intermediate waypoints on a straight
// corridor, so a closed door mid-corridor is never a Path step. The
// executor must detect it on its own. Asserts ≥1 Interact aimed at the
// door column, entity passes through, emits stay under the cooldown cap.
static std::string p14_closed_door_straight_corridor() {
	struct MutableDoor {
		glm::ivec3 feet;
		bool       open = false;
	};
	struct FakeWorld : public WorldView {
		const MutableDoor* door;
		explicit FakeWorld(const MutableDoor* d) : door(d) {}
		bool isSolid(glm::ivec3 p) const override {
			if (p.y <= -1) return true;
			// Doors always non-solid to the planner (matches ChunkWorldView);
			// physics collision is modeled separately in the sim loop.
			return false;
		}
	};
	struct FakeDoors : public DoorOracle {
		const MutableDoor* door;
		explicit FakeDoors(const MutableDoor* d) : door(d) {}
		bool isClosedDoor(glm::ivec3 p) const override {
			if (door->open) return false;
			return p == door->feet
			    || p == door->feet + glm::ivec3(0, 1, 0);
		}
		bool isOpenDoor(glm::ivec3 p) const override {
			if (!door->open) return false;
			return p == door->feet
			    || p == door->feet + glm::ivec3(0, 1, 0);
		}
	};

	MutableDoor door{glm::ivec3{5, 0, 0}, /*open=*/false};
	FakeWorld   world(&door);
	FakeDoors   doors(&door);
	GridPlanner planner(world);

	Path path = planner.plan({0, 0, 0}, {10, 0, 0});
	if (path.partial || path.steps.empty())
		return "planner failed to find path across closed-door corridor";

	printf("\n    plan: steps=%zu partial=%d\n",
	       path.steps.size(), path.partial ? 1 : 0);
	bool doorInPath = false;
	for (const auto& w : path.steps) {
		printf("      wp (%d,%d,%d) %s\n",
		       w.pos.x, w.pos.y, w.pos.z, toString(w.kind));
		if (w.pos == door.feet) doorInPath = true;
	}

	constexpr EntityId kEid = 99;
	PathExecutor exec(&doors);
	exec.setWorldView(&world);
	exec.setPath(kEid, path);

	glm::vec3 pos{0.5f, 0.0f, 0.5f};
	int        interactEmits        = 0;
	int        firstInteractTick    = -1;
	glm::ivec3 interactTarget{INT_MIN, INT_MIN, INT_MIN};
	int        ticksUsed            = 0;

	for (int t = 0; t < 600; ++t) {
		ticksUsed = t;
		auto intent = exec.tick(kEid, pos);

		if (intent.kind == PathExecutor::Intent::Interact) {
			if (firstInteractTick < 0) firstInteractTick = t;
			interactEmits++;
			if (!intent.interactPos.empty())
				interactTarget = intent.interactPos.front();
			door.open = !door.open;   // server-side toggle
			continue;
		}
		if (intent.kind == PathExecutor::Intent::None) break;

		glm::vec3 d = intent.target - pos;
		d.y = 0;
		float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len > 1e-6f) {
			glm::vec3 stepVec = d / len * 0.1f;
			glm::vec3 next = pos + stepVec;
			if (!door.open && next.x > (float)door.feet.x - 0.01f)
				next.x = (float)door.feet.x - 0.01f;
			pos = next;
		}
		if (pos.x >= (float)door.feet.x + 1.0f) break;
	}

	printf("    sim: pos=(%.2f,%.2f,%.2f) ticks=%d interactEmits=%d "
	       "firstInteract@%d doorOpen=%d remaining=%zu\n",
	       pos.x, pos.y, pos.z, ticksUsed, interactEmits,
	       firstInteractTick, door.open ? 1 : 0,
	       exec.path(kEid).steps.size());
	printf("    door in waypoints: %s  (compression: %s)\n",
	       doorInPath ? "YES" : "NO",
	       doorInPath ? "preserved" : "dropped — executor-scan case");

	if (interactEmits < 1) {
		return "executor never emitted Interact — stall-scan regressed";
	}
	if (interactTarget != door.feet
	    && interactTarget != door.feet + glm::ivec3(0, 1, 0)) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"Interact targeted (%d,%d,%d), expected door column at (%d,%d,%d)",
			interactTarget.x, interactTarget.y, interactTarget.z,
			door.feet.x, door.feet.y, door.feet.z);
		return buf;
	}
	if (pos.x < (float)door.feet.x + 1.0f) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"entity did not pass the door cell: final x=%.2f (want ≥ %.2f)",
			pos.x, (float)door.feet.x + 1.0f);
		return buf;
	}
	// Upper-bound spam: at most one emit per cooldown window.
	int cap = 1 + ticksUsed / PathExecutor::kInteractCooldownTicks;
	if (interactEmits > cap) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"Interact spam: %d emits in %d ticks (cooldown cap %d)",
			interactEmits, ticksUsed, cap);
		return buf;
	}
	return "";
}

// ── P15 — Multi-slab door: open whole cluster + auto-close on exit ────────
// 2-wide doorway at x=5 (cells (5,0,0) and (5,0,1), each 2 blocks tall).
// Asserts: (a) the open Interact carries both feet-level slabs in the
// cluster, (b) a *second* Interact fires after the entity walks past and
// distance to every slab exceeds kDoorCloseDistance, (c) that close
// Interact also targets both slabs, (d) the door ends the sim closed.
static std::string p15_multi_slab_open_and_autoclose() {
	struct Doors2Wide {
		glm::ivec3 a, b;          // feet cells of the two slabs
		bool       open = false;
	};
	struct FakeWorld : public WorldView {
		bool isSolid(glm::ivec3 p) const override { return p.y <= -1; }
	};
	struct FakeDoors : public DoorOracle {
		const Doors2Wide* d;
		explicit FakeDoors(const Doors2Wide* x) : d(x) {}
		bool isPart(glm::ivec3 p) const {
			return p == d->a || p == d->a + glm::ivec3(0, 1, 0)
			    || p == d->b || p == d->b + glm::ivec3(0, 1, 0);
		}
		bool isClosedDoor(glm::ivec3 p) const override {
			return !d->open && isPart(p);
		}
		bool isOpenDoor(glm::ivec3 p) const override {
			return d->open && isPart(p);
		}
	};

	Doors2Wide doors{ glm::ivec3{5, 0, 0}, glm::ivec3{5, 0, 1}, /*open=*/false };
	FakeWorld   world;
	FakeDoors   oracle(&doors);
	GridPlanner planner(world);

	Path path = planner.plan({0, 0, 0}, {10, 0, 0});
	if (path.partial || path.steps.empty())
		return "planner failed across 2-wide door corridor";

	printf("\n    plan: steps=%zu partial=%d (door cluster: %s + %s)\n",
	       path.steps.size(), path.partial ? 1 : 0,
	       "(5,0,0)", "(5,0,1)");

	constexpr EntityId kEid = 99;
	PathExecutor exec(&oracle);
	exec.setWorldView(&world);
	exec.setPath(kEid, path);

	glm::vec3 pos{0.5f, 0.0f, 0.5f};
	int    openEmits = 0, closeEmits = 0;
	int    openTick = -1, closeTick = -1;
	size_t openCluster = 0, closeCluster = 0;
	bool   bothInOpen = false, bothInClose = false;
	int    ticksUsed = 0;

	auto bothSlabsCovered = [&](const std::vector<glm::ivec3>& v) {
		bool a = false, b = false;
		for (auto& p : v) {
			if (p == doors.a || p == doors.a + glm::ivec3(0, 1, 0)) a = true;
			if (p == doors.b || p == doors.b + glm::ivec3(0, 1, 0)) b = true;
		}
		return a && b;
	};

	for (int t = 0; t < 2400; ++t) {
		ticksUsed = t;
		auto intent = exec.tick(kEid, pos);

		if (intent.kind == PathExecutor::Intent::Interact) {
			if (!doors.open) {
				openEmits++;
				if (openTick < 0) openTick = t;
				openCluster = intent.interactPos.size();
				bothInOpen  = bothSlabsCovered(intent.interactPos);
				doors.open  = true;            // server-side toggle
			} else {
				closeEmits++;
				if (closeTick < 0) closeTick = t;
				closeCluster = intent.interactPos.size();
				bothInClose  = bothSlabsCovered(intent.interactPos);
				doors.open   = false;
			}
			continue;
		}
		if (intent.kind == PathExecutor::Intent::None) break;

		glm::vec3 d = intent.target - pos;
		d.y = 0;
		float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len > 1e-6f) {
			glm::vec3 stepVec = d / len * 0.1f;
			glm::vec3 next    = pos + stepVec;
			// Closed-door wall ONLY applies on approach. Once the entity
			// has crossed the door cell once, a re-close behind it must
			// not snap it backward — that's the auto-close case under test.
			if (!doors.open && pos.x < (float)doors.a.x
			    && next.x > (float)doors.a.x - 0.01f)
				next.x = (float)doors.a.x - 0.01f;
			pos = next;
		}
	}

	printf("    sim: pos=(%.2f,%.2f,%.2f) ticks=%d "
	       "openEmits=%d@t%d cluster=%zu both=%d  "
	       "closeEmits=%d@t%d cluster=%zu both=%d  doorOpen=%d\n",
	       pos.x, pos.y, pos.z, ticksUsed,
	       openEmits, openTick, openCluster, bothInOpen ? 1 : 0,
	       closeEmits, closeTick, closeCluster, bothInClose ? 1 : 0,
	       doors.open ? 1 : 0);

	if (openEmits == 0)
		return "no open Interact emitted";
	if (openCluster < 2)
		return "open cluster < 2 — second slab not BFS'd";
	if (!bothInOpen)
		return "open cluster missing one of the 2-wide slabs";
	if (closeEmits == 0)
		return "no close Interact after entity walked past — auto-close regressed";
	if (closeCluster < 2)
		return "close cluster < 2 — second slab not in openedDoors";
	if (!bothInClose)
		return "close cluster missing one of the 2-wide slabs";
	if (doors.open)
		return "door left open at sim end";
	return "";
}

// ── P16 — Auto-close politeness: don't slam in someone else's face ────────
// Same 2-wide doorway as P15. After entity walks through, a *second*
// entity stands at the doorway. The auto-close must DEFER while that
// blocker is present, then fire as soon as the blocker steps away.
// Asserts: (1) no close while blocker is within kDoorPolitenessRadius,
// (2) close fires within ~1 cooldown window after blocker leaves,
// (3) door ends closed.
static std::string p16_autoclose_waits_for_blocker() {
	struct Doors2Wide { glm::ivec3 a, b; bool open = false; };
	struct FakeWorld  : public WorldView {
		bool isSolid(glm::ivec3 p) const override { return p.y <= -1; }
	};
	struct FakeDoors : public DoorOracle {
		const Doors2Wide* d;
		explicit FakeDoors(const Doors2Wide* x) : d(x) {}
		bool isPart(glm::ivec3 p) const {
			return p == d->a || p == d->a + glm::ivec3(0, 1, 0)
			    || p == d->b || p == d->b + glm::ivec3(0, 1, 0);
		}
		bool isClosedDoor(glm::ivec3 p) const override { return !d->open && isPart(p); }
		bool isOpenDoor  (glm::ivec3 p) const override { return  d->open && isPart(p); }
	};
	// Blocker position is mutated by the sim — when it's "present" we
	// place it right in the doorway; "absent" we move it far away so
	// the radius check is unambiguously clear.
	struct MutableBlocker { glm::vec3 pos{1000.f, 0.f, 1000.f}; };
	struct FakeProx : public EntityProximityOracle {
		const MutableBlocker* b;
		explicit FakeProx(const MutableBlocker* x) : b(x) {}
		bool entityNearAny(const std::vector<glm::ivec3>& cells, float radius,
		                   EntityId /*self*/) const override {
			float r2 = radius * radius;
			for (auto& c : cells) {
				float dx = b->pos.x - ((float)c.x + 0.5f);
				float dz = b->pos.z - ((float)c.z + 0.5f);
				if (dx * dx + dz * dz < r2) return true;
			}
			return false;
		}
	};

	Doors2Wide      doors{ glm::ivec3{5, 0, 0}, glm::ivec3{5, 0, 1}, false };
	FakeWorld       world;
	FakeDoors       oracle(&doors);
	MutableBlocker  blocker;
	FakeProx        prox(&blocker);
	GridPlanner     planner(world);

	Path path = planner.plan({0, 0, 0}, {10, 0, 0});
	if (path.partial || path.steps.empty())
		return "planner failed across 2-wide door corridor";

	constexpr EntityId kEid = 99;
	PathExecutor exec(&oracle);
	exec.setWorldView(&world);
	exec.setEntityProximityOracle(&prox);
	exec.setPath(kEid, path);

	glm::vec3 pos{0.5f, 0.0f, 0.5f};
	int  closeEmits = 0;
	int  closeTick  = -1;
	int  blockerArrivedAt = -1;
	int  blockerLeftAt    = -1;
	bool blockerPresent = false;
	int  closeAttemptsWhileBlocked = 0;
	int  ticksUsed = 0;

	for (int t = 0; t < 2400; ++t) {
		ticksUsed = t;

		// Mid-sim event sequence: as soon as the entity has cleared the
		// door cell, plant a blocker at the doorway. Hold for 200 ticks
		// (well over the 15-tick cooldown so we can confirm DEFERRAL,
		// not just luck of timing). Then move the blocker far away so
		// the close should fire shortly after.
		if (!blockerPresent && pos.x > (float)doors.a.x + 0.5f
		    && doors.open && closeTick < 0) {
			blocker.pos = { (float)doors.a.x + 0.5f, 0.0f,
			                (float)doors.a.z + 0.5f };
			blockerPresent  = true;
			blockerArrivedAt = t;
		}
		if (blockerPresent && blockerArrivedAt >= 0
		    && t - blockerArrivedAt > 200) {
			blocker.pos = {1000.0f, 0.0f, 1000.0f};
			blockerLeftAt = t;
			blockerPresent = false;
		}

		auto intent = exec.tick(kEid, pos);

		if (intent.kind == PathExecutor::Intent::Interact) {
			if (!doors.open) {
				doors.open = true;     // open
			} else {
				if (closeTick < 0) closeTick = t;
				closeEmits++;
				doors.open = false;
			}
			continue;
		}

		// Hidden bookkeeping: count *attempted* close windows while blocked.
		// A "close attempt" is when interactCooldown==0 + entity is past the
		// door + door is open. The executor checks all this internally; we
		// approximate from outside by counting eligible ticks.
		if (blockerPresent && doors.open
		    && pos.x > (float)doors.a.x + (float)PathExecutor::kDoorCloseDistance + 0.5f) {
			closeAttemptsWhileBlocked++;
		}

		if (intent.kind == PathExecutor::Intent::None) {
			// If we've finished walking but the close hasn't fired yet
			// (e.g. blocker still standing there), let the executor keep
			// ticking. setPath is exhausted but openedDoors stays set.
			if (closeTick < 0) continue;
			break;
		}

		glm::vec3 d = intent.target - pos;
		d.y = 0;
		float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len > 1e-6f) {
			glm::vec3 stepVec = d / len * 0.1f;
			glm::vec3 next    = pos + stepVec;
			if (!doors.open && pos.x < (float)doors.a.x
			    && next.x > (float)doors.a.x - 0.01f)
				next.x = (float)doors.a.x - 0.01f;
			pos = next;
		}
	}

	printf("    sim: pos=(%.2f,%.2f,%.2f) ticks=%d "
	       "blockerArrived@%d blockerLeft@%d closeAttemptsWhileBlocked=%d "
	       "closeEmits=%d closeTick=%d doorOpen=%d\n",
	       pos.x, pos.y, pos.z, ticksUsed,
	       blockerArrivedAt, blockerLeftAt, closeAttemptsWhileBlocked,
	       closeEmits, closeTick, doors.open ? 1 : 0);

	if (blockerArrivedAt < 0)
		return "test setup: blocker was never planted";
	if (closeAttemptsWhileBlocked < 5)
		return "test setup: not enough eligible close-attempt ticks while blocked";
	if (closeTick >= 0 && blockerLeftAt < 0)
		return "close fired before blocker was even removed — politeness gate failed";
	if (closeTick >= 0 && closeTick < blockerLeftAt)
		return "close fired while blocker was still present — politeness gate failed";
	if (closeEmits == 0)
		return "close never fired even after blocker left";
	if (doors.open)
		return "door left open at sim end";
	// Sanity: close should fire within ~2 cooldown windows of the blocker leaving.
	int latency = closeTick - blockerLeftAt;
	if (latency > 2 * PathExecutor::kInteractCooldownTicks + 5) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"close-after-clear latency too high: %d ticks (cap %d)",
			latency, 2 * PathExecutor::kInteractCooldownTicks + 5);
		return buf;
	}
	return "";
}

} // namespace civcraft::test

int main() {
	using namespace civcraft;
	using namespace civcraft::test;

	pythonBridge().init("python");

	printf("\n=== CivCraft Pathfinding Tests ===\n\n");
	initTemplates();

	printf("--- Harness ---\n");
	run("P01: harness smoke (player stays on floor)", p01_harness_smoke);

	// P02/P03/P06/P09 dropped: they drove entities via server-side nav
	// (`e->nav.setGoal()`), now removed. End-to-end drive moves to the
	// client smoke run + civcraft_engine.Navigator tests (Phase D).

	printf("\n--- Planner (GridPlanner::plan unit test) ---\n");
	run("P04: stairs-up plan (3 Jumps)                ", p04_stairs_up_plan);
	run("P05: bug-trap plan (concave pocket, detour)  ", p05_bug_trap_plan);

	printf("\n--- Batch planning (RTS group command, SupCom2-style) ---\n");
	run("P07: planBatch — shared goal, 3 starts       ", p07_plan_batch_shared_goal);

	printf("\n--- Replan triggers ---\n");
	run("P08: pathInvalidatedBy corridor detection    ", p08_path_invalidation);

	printf("\n--- RTS planner tunnels ---\n");
	run("P10: narrow ] [ tunnel (penalty>0)            ", p10_narrow_tunnel_still_passes);

	printf("\n--- Perf scaling (N=1..1000) ---\n");
	run("P11: planGroup time at scale                 ", p11_perf_scaling);

	printf("\n--- PathExecutor direction stability ---\n");
	run("P12: no opposite moveTargets back-to-back    ", p12_no_opposite_movetargets);
	run("P13: half-plane retire on teleport-forward   ", p13_teleport_past_front);

	printf("\n--- Door handling ---\n");
	run("P14: stall-scan opens off-path closed door   ", p14_closed_door_straight_corridor);
	run("P15: 2-wide door open cluster + auto-close   ", p15_multi_slab_open_and_autoclose);
	run("P16: auto-close defers for blocker in doorway", p16_autoclose_waits_for_blocker);

	int failed = 0;
	for (auto& r : g_results) if (!r.passed) failed++;
	printf("\n%d/%zu passed\n", (int)g_results.size() - failed, g_results.size());
	return failed;
}

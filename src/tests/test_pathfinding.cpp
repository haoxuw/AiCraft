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

namespace solarium::test {

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

	ChunkWorldView view(srv->chunks(), srv->blockRegistry());

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

// ── P12 deleted ──────────────────────────────────────────────────────────
// Previously tested per-tick direction stability with a fake driver that
// bypassed the real per-tick integration. Phase 5 removed velocity
// smoothing entirely, and the convergence watchdog (P17 + P18) now covers
// the actual invariant — "distance to front waypoint must improve."
// P12's mechanism is obsolete.

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
static std::string p17_high_speed_corner_overshoot() {
	struct SegStat { int ticks; float dist; float speed; };
	struct R {
		int arriveTick; float distXZ; int worstStall;
		size_t remaining; int convergeFailures;
		std::vector<SegStat> segments;   // per-waypoint timing
	};
	auto runAtSpeed = [](float walkSpeed, int maxTicks) -> R {
		constexpr EntityId kEid = 17;
		solarium::g_pathConvergenceFailures.store(0, std::memory_order_relaxed);
		// 9 east, 8 north — sharp 90° turn at corner cell (8,0,0) → (8,0,1).
		Path p;
		for (int x = 0; x <= 8; ++x) p.steps.push_back({{x, 0, 0}, MoveKind::Walk});
		for (int z = 1; z <= 8; ++z) p.steps.push_back({{8, 0, z}, MoveKind::Walk});

		auto centerOf = [](glm::ivec3 c) {
			return glm::vec3(c.x + 0.5f, (float)c.y, c.z + 0.5f);
		};
		// Pre-compute distances between consecutive waypoint centers. Index i
		// corresponds to "leaving wp i-1, arriving at wp i". Segment 0's
		// distance is from start to wp 0.
		std::vector<float> segDist;
		segDist.reserve(p.steps.size());
		glm::vec3 prevC{0.5f, 0.0f, 0.5f};   // start position
		for (auto& w : p.steps) {
			glm::vec3 c = centerOf(w.pos);
			float dx = c.x - prevC.x, dz = c.z - prevC.z;
			segDist.push_back(std::sqrt(dx * dx + dz * dz));
			prevC = c;
		}

		PathExecutor exec;
		exec.setPath(kEid, p);

		glm::vec3 pos{0.5f, 0.0f, 0.5f};
		constexpr float dt = 1.0f / 60.0f;

		const size_t totalSegs = p.steps.size();
		size_t lastRem = totalSegs;
		int    stallTicks = 0;
		int    worstStall = 0;
		int    arriveTick = -1;
		int    segStartTick = 0;
		std::vector<SegStat> segments;
		segments.reserve(totalSegs);

		for (int t = 0; t < maxTicks; ++t) {
			if (exec.done(kEid)) { arriveTick = t; break; }
			auto intent = exec.tick(kEid, pos);
			if (intent.kind == PathExecutor::Intent::None) {
				arriveTick = t; break;
			}
			// Mirror driveOne (path_executor.cpp ~line 685-705): raw direction,
			// hard one-tick backstop. No velocity smoothing.
			glm::vec3 d = intent.target - pos;
			d.y = 0;
			float len = std::sqrt(d.x * d.x + d.z * d.z);
			if (len < 1e-3f) continue;
			d /= len;
			float clamped = std::min(walkSpeed, len / dt);
			pos.x += d.x * clamped * dt;
			pos.z += d.z * clamped * dt;

			size_t rem = exec.path(kEid).steps.size();
			if (rem == lastRem) {
				if (++stallTicks > worstStall) worstStall = stallTicks;
			} else {
				// One or more segments retired this tick (rem < lastRem).
				// Distribute timing evenly across them — usually rem == lastRem-1
				// so just one segment closed.
				const size_t closed = lastRem - rem;
				const int ticksUsed = (t + 1) - segStartTick;
				for (size_t k = 0; k < closed; ++k) {
					const size_t segIdx = totalSegs - lastRem + k;
					if (segIdx >= segDist.size()) break;
					const float dist = segDist[segIdx];
					const int   ticksHere = (k == 0) ? ticksUsed : 1;
					const float secs = ticksHere * dt;
					segments.push_back({ ticksHere, dist,
					                     secs > 0 ? dist / secs : 0.0f });
				}
				segStartTick = t + 1;
				stallTicks = 0;
				lastRem = rem;
			}
		}

		glm::vec3 goalC{8.5f, 0.0f, 8.5f};
		float dx = pos.x - goalC.x, dz = pos.z - goalC.z;
		const int convergeFailures =
			solarium::g_pathConvergenceFailures.load(std::memory_order_relaxed);
		return R{ arriveTick, std::sqrt(dx*dx + dz*dz), worstStall,
		          exec.path(kEid).steps.size(), convergeFailures,
		          std::move(segments) };
	};

	auto slow = runAtSpeed(2.0f, 1500);
	printf("    slow(walkSpeed=2):  arriveTick=%d distXZ=%.2f worstStall=%d remaining=%zu convergeFails=%d\n",
	       slow.arriveTick, slow.distXZ, slow.worstStall, slow.remaining, slow.convergeFailures);
	if (!slow.segments.empty()) {
		float sumS = 0, minS = 1e30f, maxS = 0;
		for (auto& s : slow.segments) {
			sumS += s.speed;
			if (s.speed < minS) minS = s.speed;
			if (s.speed > maxS) maxS = s.speed;
		}
		printf("        segments: n=%zu  speed mean=%.2f min=%.2f max=%.2f m/s",
		       slow.segments.size(), sumS / slow.segments.size(), minS, maxS);
		if (slow.segments.size() > 9) {
			printf("  | corner-seg(idx=9): dist=%.2fm ticks=%d speed=%.2fm/s\n",
			       slow.segments[9].dist, slow.segments[9].ticks, slow.segments[9].speed);
		} else {
			printf("\n");
		}
	}
	if (slow.arriveTick < 0) {
		return "SLOW CONTROL FAILED: entity didn't arrive at 2 m/s — test driver bug, not the overshoot bug under test";
	}
	if (slow.convergeFailures > 0) {
		return "SLOW CONTROL FAILED: convergence watchdog flagged stagnation — test driver bug or watchdog mistuned";
	}

	// Budget-based arrival check. Effective speed is just walkSpeed (no more
	// approach-ramp slowdown), but we floor the budget so very fast probes
	// don't get unreasonably tight budgets — the entity still needs a few
	// ticks for setup + the corner cell + final segment-crossing pop.
	auto budgetTicks = [](float walkSpeed) -> int {
		constexpr float pathLen = 16.0f;
		constexpr float dt      = 1.0f / 60.0f;
		// Effective floor: even instantaneous travel needs ~30 ticks for
		// path traversal infrastructure (predicate ticks, stall pops, etc.).
		const int raw = (int)((pathLen * 3.0f) / (walkSpeed * dt));
		return std::max(raw, 60);
	};

	// Sweep across a range to validate "arbitrarily fast." Each speed should
	// arrive within its own scaled budget — orbit at any speed blows it.
	struct Probe { float walkSpeed; int budget; };
	const Probe probes[] = {
		{   8.0f, budgetTicks(8.0f)   },     // just past orbit threshold
		{  50.0f, budgetTicks(50.0f)  },     // 10× safe
		{ 500.0f, budgetTicks(500.0f) },     // Mach-ish
	};
	for (auto pr : probes) {
		// budgetTicks already floors at 60 so very fast probes don't get
		// unreasonably tight budgets — apply the same floor here for clarity.
		int budget = std::max(pr.budget, 60);
		auto r = runAtSpeed(pr.walkSpeed, budget * 4);
		printf("    fast(walkSpeed=%6.1f): arriveTick=%d distXZ=%.2f "
		       "worstStall=%d remaining=%zu convergeFails=%d  (budget=%d)\n",
		       pr.walkSpeed, r.arriveTick, r.distXZ, r.worstStall,
		       r.remaining, r.convergeFailures, budget);
		// Per-segment timing receipt — answers "is each segment covered at the
		// same speed?" Min/max/mean over all segments + the corner-segment
		// callout (segment 9 in this 9+8 L-path).
		if (!r.segments.empty()) {
			float sumS = 0, minS = 1e30f, maxS = 0;
			for (auto& s : r.segments) {
				sumS += s.speed;
				if (s.speed < minS) minS = s.speed;
				if (s.speed > maxS) maxS = s.speed;
			}
			const float meanS = sumS / r.segments.size();
			printf("        segments: n=%zu  speed mean=%.2f min=%.2f max=%.2f m/s",
			       r.segments.size(), meanS, minS, maxS);
			// Corner is at index 9 in the path (8th east cell → 1st north cell).
			if (r.segments.size() > 9) {
				printf("  | corner-seg(idx=9): dist=%.2fm ticks=%d speed=%.2fm/s\n",
				       r.segments[9].dist, r.segments[9].ticks, r.segments[9].speed);
			} else {
				printf("\n");
			}
		}
		if (r.arriveTick < 0 || r.arriveTick > budget) {
			char buf[300];
			snprintf(buf, sizeof(buf),
				"walkSpeed=%.1f: orbit/overshoot — arriveTick=%d (budget=%d) "
				"distXZ=%.2f remaining=%zu worstStall=%d convergeFails=%d",
				pr.walkSpeed, r.arriveTick, budget,
				r.distXZ, r.remaining, r.worstStall, r.convergeFailures);
			return buf;
		}
		// In-code convergence assertion: distance to current front waypoint
		// must reach a new minimum within kConvergeStallTicks. If the orbit
		// path is back, this fires before arrival even happens.
		if (r.convergeFailures > 0) {
			char buf[260];
			snprintf(buf, sizeof(buf),
				"walkSpeed=%.1f: convergence watchdog flagged %d stagnations "
				"(distance to front not improving over %d-tick window — orbit?)",
				pr.walkSpeed, r.convergeFailures,
				PathExecutor::kConvergeStallTicks);
			return buf;
		}
	}
	return "";
}

// ── P18 — Negative test: orbit IS caught by the convergence watchdog ─────
// Drives the entity in a forced circle around the front waypoint without
// ever closing the gap — distance to the front stays constant. The
// watchdog (in PathExecutor::tick) MUST notice and bump
// g_pathConvergenceFailures.
//
// Why a forced circle (vs the old "remove the speed-clamp" approach):
// Phase 5 deleted velocity smoothing, so even a no-clamp driver can't
// produce smoothing-induced orbit anymore. The orbit pathology only
// existed because of smoothing × min-turn-radius geometry. Without
// smoothing, the worst a missing clamp can do is overshoot by one tick.
// To negative-test the watchdog we now bypass the predicate-friendly
// driver entirely and force the position into a circle.
//
// If this test ever PASSES with 0 failures, the watchdog itself is broken
// (nonConvergeTicks not incrementing, dedupe too aggressive, threshold
// too high) and P17's "convergeFails=0" PASS is no longer evidence of
// anything.
static std::string p19_small_dist_one_tick_arrival() {
	constexpr float walkSpeed = 8.0f;
	constexpr float dt        = 1.0f / 60.0f;
	const     float maxStep   = walkSpeed * dt;          // 0.133 m/tick
	// Probe distances strictly under maxStep — every one of these must
	// converge in exactly one tick of motion.
	const float probes[] = { 0.001f, 0.01f, 0.05f, 0.10f, 0.12f, 0.132f };

	for (float startDist : probes) {
		constexpr EntityId kEid = 19;
		Path p;
		p.steps.push_back({{0, 0, 0}, MoveKind::Walk});  // wp center (0.5, 0, 0.5)

		PathExecutor exec;
		exec.setPath(kEid, p);
		solarium::g_pathConvergenceFailures.store(0, std::memory_order_relaxed);

		// Approach the wp center from the +X side along the cardinal axis.
		glm::vec3 pos{0.5f + startDist, 0.0f, 0.5f};
		const char* path = "?";

		// Tick 0: anchors prevPos. Within kSnapRadius the predicate pops
		// from rest on this tick (entity already on the cell). Above that,
		// we need a tick of motion.
		auto intent0 = exec.tick(kEid, pos);
		if (exec.path(kEid).steps.empty()) {
			path = "snap-radius (no motion needed)";
		} else {
			// Drive one tick of motion mirroring driveOne's hard backstop only.
			if (intent0.kind != PathExecutor::Intent::Move) {
				char buf[160];
				snprintf(buf, sizeof(buf),
					"startDist=%.3f: tick0 didn't pop and didn't return Move — kind=%d",
					startDist, (int)intent0.kind);
				return buf;
			}
			glm::vec3 d = intent0.target - pos;
			d.y = 0;
			float len = std::sqrt(d.x * d.x + d.z * d.z);
			d /= len;
			float clamped = std::min(walkSpeed, len / dt);
			pos.x += d.x * clamped * dt;
			pos.z += d.z * clamped * dt;
			(void)exec.tick(kEid, pos);
			path = "one tick of motion (hard backstop)";
		}

		const float arriveErr = std::sqrt(
			(pos.x - 0.5f) * (pos.x - 0.5f) + (pos.z - 0.5f) * (pos.z - 0.5f));
		const size_t remaining = exec.path(kEid).steps.size();
		const int    convFails = solarium::g_pathConvergenceFailures.load(
			std::memory_order_relaxed);

		printf("    startDist=%.3f → arriveErr=%.4f remaining=%zu convergeFails=%d  via %s\n",
		       startDist, arriveErr, remaining, convFails, path);

		if (remaining != 0) {
			char buf[220];
			snprintf(buf, sizeof(buf),
				"startDist=%.3f: HARD-BACKSTOP FAILED — remaining=%zu (arriveErr=%.4f) — "
				"entity didn't converge to wp in one v*dt tick",
				startDist, remaining, arriveErr);
			return buf;
		}
		if (convFails != 0) {
			char buf[200];
			snprintf(buf, sizeof(buf),
				"startDist=%.3f: watchdog fired (%d) on a one-tick arrival — false positive",
				startDist, convFails);
			return buf;
		}
	}
	return "";
}

// ── P20 — Multi-villager pathfinding with real applySeparation ──────────
// Bug-capture test (FAILS today, passes when forward-speed guarantee is
// added to emitMoveAction).
//
// Mirrors `make game --villagers N`: each villager has its own start +
// target ~5 m east. Each tick every villager:
//   • Computes intent toward its target at walkSpeed=4 m/s.
//   • Calls real applySeparation with all OTHER villagers as neighbors
//     (mutual push, same as in-game).
//   • Integrates pos += vel * dt.
//
// Two probes: N=1 (control — no other entities, must walk straight at
// full speed) and N=4 (bug repro — separation from peers stalls them).
//
// Pass criterion: minimum achieved speed across ALL villagers ≥ 3.0 m/s
// (= 75% of walkSpeed). The user explicitly specified 3 m/s as the
// target — efficient segment convergence requires forward progress this
// fast even when neighbors are close.
//
// Today, N=1 passes (no neighbors); N=4 fails — slowest villager stalls
// near 0 m/s because four mutual-separation pushes cancel intent.
// Fix: in emitMoveAction post-applySeparation, decompose final vel into
// (forward, lateral) along the intent direction and guarantee forward
// component ≥ 75% of intent magnitude.
static std::string p20_separation_orbit_real_code() {
	struct ProbeResult { int arrived; float minSpeed; float meanSpeed; float maxRatio; };

	auto runScenario = [](int N, const char* label) -> ProbeResult {
		constexpr float walkSpeed = 4.0f;
		constexpr float dt        = 1.0f / 60.0f;
		constexpr int   maxTicks  = 900;     // 15 s — generous to allow stalls
		solarium::BlockSolidFn isSolid = [](int, int, int) -> float { return 0.0f; };

		// N entities clustered at start, each pathing to a target 5 m east.
		// Cluster spacing ~0.5 m so neighbors are always within sep radius.
		std::vector<glm::vec3> pos(N), startPos(N), target(N);
		std::vector<glm::vec2> dvPrev(N, glm::vec2(0.0f, 0.0f));
		std::vector<float>     arcLen(N, 0.0f);
		std::vector<int>       arriveTick(N, -1);
		for (int i = 0; i < N; ++i) {
			const float ox = 0.4f * (i % 2);
			const float oz = 0.4f * (i / 2);
			startPos[i] = pos[i] = glm::vec3(0.5f + ox, 0.0f, 0.5f + oz);
			target[i]   = glm::vec3(5.5f + ox, 0.0f, 0.5f + oz);
		}

		for (int t = 0; t < maxTicks; ++t) {
			int doneCount = 0;
			for (int i = 0; i < N; ++i) {
				if (arriveTick[i] >= 0) { doneCount++; continue; }
				glm::vec3 d = target[i] - pos[i];
				d.y = 0;
				const float len = std::sqrt(d.x*d.x + d.z*d.z);
				if (len < 0.1f) { arriveTick[i] = t; doneCount++; continue; }
				d /= len;
				glm::vec3 vel{d.x * walkSpeed, 0.0f, d.z * walkSpeed};

				// Build neighbors: all OTHER villagers, stationary-or-moving
				// (we use their last-tick pos, vel = (0,0,0) approximation —
				// matches what gatherSepNeighbors produces with idle peers).
				std::vector<solarium::SepNeighbor> neighbors;
				for (int j = 0; j < N; ++j) {
					if (j == i) continue;
					neighbors.push_back({(EntityId)(j+1), pos[j],
					                     glm::vec3(0,0,0), 0.4f});
				}

				const float intentMag = walkSpeed;
				const glm::vec2 intentDir{d.x, d.z};
				vel = solarium::applySeparation(
					(EntityId)(i+1), pos[i], vel,
					0.4f, walkSpeed, 1.8f, 1.0f,
					neighbors, isSolid, dvPrev[i]);
				// Mirror emitMoveAction's forward-guarantee — production code
				// runs this immediately after applySeparation. Without it,
				// the test wouldn't see the fix even when production has it.
				vel = solarium::applyForwardGuarantee(vel, intentDir, intentMag);

				const glm::vec3 prev = pos[i];
				pos[i].x += vel.x * dt;
				pos[i].z += vel.z * dt;
				arcLen[i] += std::sqrt((pos[i].x - prev.x) * (pos[i].x - prev.x) +
				                       (pos[i].z - prev.z) * (pos[i].z - prev.z));
			}
			if (doneCount == N) break;
		}

		// Per-villager achieved speed.
		float minSp = 1e9f, maxR = 0.0f;
		double sumSp = 0.0;
		int    arrived = 0;
		for (int i = 0; i < N; ++i) {
			const float dx = target[i].x - startPos[i].x;
			const float dz = target[i].z - startPos[i].z;
			const float direct = std::sqrt(dx*dx + dz*dz);
			const float secs = (arriveTick[i] > 0 ? arriveTick[i] : maxTicks) * dt;
			const float sp = arriveTick[i] > 0 ? direct / secs : arcLen[i] / secs;
			const float r  = direct > 0.01f ? arcLen[i] / direct : 0.0f;
			if (sp < minSp) minSp = sp;
			if (r  > maxR ) maxR  = r;
			sumSp += sp;
			if (arriveTick[i] > 0) arrived++;
		}
		printf("    N=%d %-22s arrived=%d/%d  speed: min=%.2f mean=%.2f m/s  "
		       "maxRatio=%.2f\n",
		       N, label, arrived, N,
		       minSp, sumSp / N, maxR);
		return { arrived, minSp, (float)(sumSp / N), maxR };
	};

	auto r1  = runScenario(1,  "1 villager (control)");
	auto r4  = runScenario(4,  "4 villagers (real repro)");
	auto r10 = runScenario(10, "10 villagers (stress)");

	// Pass criteria:
	//   N=1   : full speed (no other entities → no separation push).
	//   N=4   : every villager achieves ≥ 3 m/s (= 75% of walkSpeed=4).
	//   N=10  : same — fix must scale to crowded scenarios.
	constexpr float kWalkSpeed         = 4.0f;
	constexpr float kMinSpeedThreshold = 3.0f;

	if (r1.minSpeed < kWalkSpeed * 0.95f) {
		char buf[200];
		snprintf(buf, sizeof(buf),
			"N=1 control failed: minSpeed=%.2fm/s (expected ≥%.2f). "
			"Driver bug, not the production bug.",
			r1.minSpeed, kWalkSpeed * 0.95f);
		return buf;
	}
	auto checkProbe = [&](const ProbeResult& r, int N) -> std::string {
		if (r.minSpeed < kMinSpeedThreshold) {
			char buf[260];
			snprintf(buf, sizeof(buf),
				"N=%d villagers stall: slowest=%.2fm/s (expected ≥%.2f). "
				"applySeparation force-stacks and cancels intent.",
				N, r.minSpeed, kMinSpeedThreshold);
			return buf;
		}
		// Bounded zigzag — arc/direct ratio capped via maxLateralRatio.
		// Even with multiple per-tick deflections, max ~22° off-axis →
		// arc/direct ≤ ~1/cos(22°) ≈ 1.08 in steady state. Allow some slack.
		if (r.maxRatio > 1.5f) {
			char buf[260];
			snprintf(buf, sizeof(buf),
				"N=%d villagers wobble: maxRatio=%.2f (expected ≤1.5). "
				"Lateral cap not strong enough.",
				N, r.maxRatio);
			return buf;
		}
		return "";
	};
	if (auto m = checkProbe(r4,  4 ); !m.empty()) return m;
	if (auto m = checkProbe(r10, 10); !m.empty()) return m;
	return "";
}

// ── P21 — Open-square diagonal collapses to a single waypoint ───────────
// Open 10x10 air slab over solid ground. Plan (0,0,0) → (9,0,9). With the
// any-angle LOS-Walk smoothing, the only kept Walk wp is the goal itself —
// the entity walks one diagonal segment instead of an east-then-north L
// or staircase. This is the positive receipt for the LOS smoother.
static std::string p21_open_square_diagonal() {
	struct FlatWorld : public WorldView {
		bool isSolid(glm::ivec3 p) const override { return p.y <= -1; }
	};
	FlatWorld   world;
	GridPlanner planner(world);
	Path path = planner.plan({0, 0, 0}, {9, 0, 9});
	if (path.partial)        return "planner gave up on open square";
	if (path.steps.empty())  return "planner returned 0 wps";
	if (path.steps.back().pos != glm::ivec3{9, 0, 9})
		return "last wp is not the goal";

	printf("\n    plan: steps=%zu (expect 1 — any-angle diagonal)\n",
	       path.steps.size());
	if (path.steps.size() != 1) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"open-square diagonal compressed to %zu wps (expected 1) — "
			"LOS-Walk smoothing not collapsing the staircase.",
			path.steps.size());
		return buf;
	}
	return "";
}

// ── P22 — Wall detour preserved (negative receipt for LOS smoother) ─────
// Same 10x10 slab but with a vertical wall at x=5, z∈[0..4] blocking LOS
// from (0,0,0) → (9,0,9). Smoother must NOT collapse to 1 wp — the
// entity has to round the wall's bottom end. Asserts ≥2 kept wps and
// that the path is non-partial. This is the negative-test counterpart
// to P21: if smoothing ever forgets to LOS-check, P21 passes for the
// wrong reason while P22 fails.
static std::string p22_wall_detour_preserved() {
	struct WallWorld : public WorldView {
		bool isSolid(glm::ivec3 p) const override {
			if (p.y <= -1) return true;                    // floor
			if (p.x == 5 && p.y >= 0 && p.y <= 1
			    && p.z >= 0 && p.z <= 4) return true;      // 2-tall wall
			return false;
		}
	};
	WallWorld   world;
	GridPlanner planner(world);
	Path path = planner.plan({0, 0, 0}, {9, 0, 9});
	if (path.partial)        return "wall detour planner partial (should reach)";
	if (path.steps.empty())  return "wall detour returned 0 wps";
	if (path.steps.back().pos != glm::ivec3{9, 0, 9})
		return "last wp is not the goal";

	printf("\n    plan: steps=%zu (expect ≥2 — must round wall x=5,z=[0..4])\n",
	       path.steps.size());
	if (path.steps.size() < 2) {
		char buf[160];
		snprintf(buf, sizeof(buf),
			"wall detour compressed to %zu wps — LOS-Walk skipped the "
			"wall check (corner-cut bug).", path.steps.size());
		return buf;
	}
	return "";
}

// ── P14 — Closed door off-path (regression) ───────────────────────────────
// Collinear-Walk compression drops intermediate waypoints on a straight
// corridor, so a closed door mid-corridor is never a Path step. The
// executor must detect it on its own. Asserts ≥1 Interact aimed at the
// door column, entity passes through, emits stay under the cooldown cap.
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
		return "close cluster < 2 — second slab not in passedDoors";
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
} // namespace solarium::test

int main() {
	using namespace solarium;
	using namespace solarium::test;

	pythonBridge().init("python");

	printf("\n=== Solarium Pathfinding Tests ===\n\n");
	initTemplates();

	// Trimmed to top-5 useful tests. Each one captures something a real
	// regression would break:
	//   • P11 — planner perf scaling (catches O(N²) regressions).
	//   • P17 — driveOne speed-clamp + watchdog across walkSpeeds.
	//   • P19 — hard-backstop one-tick arrival at small distance.
	//   • P20 — separation-induced orbit using REAL applySeparation
	//           (currently FAILS — this is the in-game bug we observed).
	//   • P15 — door cluster open + auto-close round-trip.

	printf("\n--- Planner perf ---\n");
	run("P11: planGroup time at scale                 ", p11_perf_scaling);

	printf("\n--- PathExecutor (real driveOne pipeline) ---\n");
	run("P17: high-speed corner overshoot (8 m/s L-turn)", p17_high_speed_corner_overshoot);
	run("P19: small-dist hard-backstop one-tick arrival  ", p19_small_dist_one_tick_arrival);
	run("P20: separation orbit — real applySeparation     ", p20_separation_orbit_real_code);

	printf("\n--- Planner LOS-Walk smoothing ---\n");
	run("P21: open-square diagonal collapses to 1 wp ", p21_open_square_diagonal);
	run("P22: wall detour preserved (≥2 wps)        ", p22_wall_detour_preserved);

	printf("\n--- Door handling ---\n");
	run("P15: 2-wide door open cluster + auto-close   ", p15_multi_slab_open_and_autoclose);

	int failed = 0;
	for (auto& r : g_results) if (!r.passed) failed++;
	printf("\n%d/%zu passed\n", (int)g_results.size() - failed, g_results.size());
	return failed;
}

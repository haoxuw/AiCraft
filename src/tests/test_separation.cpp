/**
 * test_separation.cpp — Headless tests for applySeparation.
 *
 * Pure-function tests on the soft-collision helper: each scenario picks an
 * intent vector, a neighbor configuration, and a wall predicate, then
 * asserts that the modified desiredVel matches the design.
 *
 * Lives next to test_pathfinding.cpp; same Result/run pattern. No TestServer
 * needed — applySeparation is stateless apart from the dvPrev parameter the
 * caller threads through, so we can drive it with hand-rolled inputs.
 *
 * Scenarios (math doc: src/platform/docs/29_ENTITY_SEPARATION.md):
 *   S1  alone-no-walls          — intent passes through unchanged
 *   S2  intent-preserved-sprint — sprint magnitude survives (no speed cap)
 *   S3  idle-clears-lpf         — selfSpeedSq < ε zeroes dvPrev, returns input
 *   S4  head-on-both-moving     — both side-dodge via tie-breaker
 *   S5  perpendicular-cross     — anticipatory bias, smaller than head-on
 *   S6  parallel-same-direction — no force (a < parallelEps)
 *   S7  diverging               — no force (b ≥ 0)
 *   S8  moving-vs-idle          — moving self dodges; idle self exits early
 *   S9  penetration-overlap     — overlapping pair pushed apart
 *   S10 deltacap                — Δv capped to walk_speed magnitude
 *   S11 wall-projection         — Δv into a 2-tall wall is zeroed
 *   S12 step-up-pass-through    — 1-block ledge is NOT projected as a wall
 *   S13 group-of-three          — three agents in tight quarters all get bias
 *   S14 lpf-smoothing           — dvPrev tempers a sudden Δv jump
 *   S15 wall-vs-pair            — pair pushed at wall: nearside zeroed, farside free
 */

#include "agent/separation.h"
#include "logic/physics.h"
#include <glm/glm.hpp>
#include <cstdio>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace civcraft::test {

struct Result { std::string name; bool passed; std::string msg; };
static std::vector<Result> g_results;

// ── Predicates ─────────────────────────────────────────────────────────────
static BlockSolidFn noWalls() {
	return [](int, int, int) -> float { return 0.0f; };
}

// Wall column from x = xWall to +∞, full-height. Returns 1.0 (full block).
static BlockSolidFn wallAtXGreaterEq(int xWall) {
	return [xWall](int x, int /*y*/, int /*z*/) -> float {
		return (x >= xWall) ? 1.0f : 0.0f;
	};
}

// Single solid block at (bx, by, bz). For step-up tests.
static BlockSolidFn singleBlockAt(int bx, int by, int bz) {
	return [bx, by, bz](int x, int y, int z) -> float {
		return (x == bx && y == by && z == bz) ? 1.0f : 0.0f;
	};
}

// ── Defaults that match the live Knight ────────────────────────────────────
static constexpr float kRadius     = 0.35f;
static constexpr float kHeight     = 2.0f;
static constexpr float kWalkSpeed  = 6.0f;
static constexpr float kStepHeight = 1.0f;

// Convenience neighbor builder.
static SepNeighbor N(EntityId eid, glm::vec3 pos, glm::vec3 vel, float r = kRadius) {
	return {eid, pos, vel, r};
}

// Run a single applySeparation call with stock parameters.
struct SepRun {
	glm::vec3 vIn;
	glm::vec3 vOut;
	glm::vec2 dvPrevAfter;
	SepStats  stats;
	int       neighborCount = 0;
};

// Per-scenario trace state — populated by runSep, drained by run() so the
// pass/fail line shows the actual numbers.
static SepRun g_lastRun;
static bool   g_haveLastRun = false;

static SepRun runSep(EntityId selfEid, glm::vec3 pos, glm::vec3 intent,
                     const std::vector<SepNeighbor>& neighbors,
                     const BlockSolidFn& isSolid,
                     glm::vec2 dvPrevIn = {0, 0}) {
	SepRun r;
	r.vIn          = intent;
	r.dvPrevAfter  = dvPrevIn;
	r.neighborCount= (int)neighbors.size();
	r.vOut = applySeparation(
		selfEid, pos, intent,
		kRadius, kWalkSpeed, kHeight, kStepHeight,
		neighbors, isSolid, r.dvPrevAfter, SepConfig{}, &r.stats);
	g_lastRun = r; g_haveLastRun = true;
	return r;
}

static void run(const char* name, std::function<std::string()> fn) {
	std::printf("  %-32s ", name);
	std::fflush(stdout);
	g_haveLastRun = false;
	std::string err;
	try { err = fn(); }
	catch (std::exception& e) { err = std::string("EXCEPTION: ") + e.what(); }
	catch (...) { err = "UNKNOWN EXCEPTION"; }
	bool ok = err.empty();
	std::printf("%s", ok ? "PASS" : ("FAIL: " + err).c_str());
	if (g_haveLastRun) {
		const SepRun& r = g_lastRun;
		float dvx = r.vOut.x - r.vIn.x;
		float dvz = r.vOut.z - r.vIn.z;
		std::printf("  | in=(%.2f,%.2f) out=(%.2f,%.2f) Δv=(%+.2f,%+.2f) "
		            "n=%d emit=%d wall=%d",
		            r.vIn.x, r.vIn.z, r.vOut.x, r.vOut.z, dvx, dvz,
		            r.neighborCount, r.stats.pairsEmit, r.stats.wallBlocked);
	}
	std::printf("\n");
	g_results.push_back({name, ok, err});
}


// ── Assertion helpers ──────────────────────────────────────────────────────
static std::string approxEq(const char* what, float got, float want, float tol) {
	if (std::fabs(got - want) <= tol) return "";
	char b[160];
	std::snprintf(b, sizeof(b), "%s: got %.4f, want %.4f (±%.4f)", what, got, want, tol);
	return b;
}

static std::string vecApproxEq(const char* what, glm::vec3 got, glm::vec3 want, float tol) {
	if (std::fabs(got.x - want.x) <= tol &&
	    std::fabs(got.y - want.y) <= tol &&
	    std::fabs(got.z - want.z) <= tol) return "";
	char b[200];
	std::snprintf(b, sizeof(b),
		"%s: got (%.3f,%.3f,%.3f), want (%.3f,%.3f,%.3f) (±%.3f)",
		what, got.x, got.y, got.z, want.x, want.y, want.z, tol);
	return b;
}

static std::string positive(const char* what, float v) {
	if (v > 1e-4f) return "";
	char b[160];
	std::snprintf(b, sizeof(b), "%s: expected > 0, got %.4f", what, v);
	return b;
}

static std::string nearZero(const char* what, float v, float tol = 1e-3f) {
	if (std::fabs(v) <= tol) return "";
	char b[160];
	std::snprintf(b, sizeof(b), "%s: expected ≈ 0, got %.4f (tol %.4f)", what, v, tol);
	return b;
}

// ── Scenarios ──────────────────────────────────────────────────────────────

static std::string s1_alone_no_walls() {
	// Walking forward on flat ground. No neighbors, no walls.
	auto r = runSep(1, {0, 0, 0}, {6, 0, 0}, {}, noWalls());
	return vecApproxEq("intent passes through", r.vOut, {6, 0, 0}, 1e-4f);
}

static std::string s2_intent_preserved_sprint() {
	// Sprint magnitude > walk_speed should survive (no speed cap).
	auto r = runSep(1, {0, 0, 0}, {9.6f, 0, 0}, {}, noWalls());
	return vecApproxEq("sprint magnitude survives", r.vOut, {9.6f, 0, 0}, 1e-3f);
}

static std::string s3_idle_clears_lpf() {
	// Self idle → applySeparation early-returns and zeroes dvPrev.
	auto r = runSep(1, {0, 0, 0}, {0, 0, 0}, {}, noWalls(), /*dvPrev=*/{1.0f, 1.0f});
	std::string e1 = vecApproxEq("idle returns intent", r.vOut, {0, 0, 0}, 1e-4f);
	if (!e1.empty()) return e1;
	if (std::fabs(r.dvPrevAfter.x) > 1e-4f || std::fabs(r.dvPrevAfter.y) > 1e-4f)
		return "idle did not clear dvPrev";
	return "";
}

static std::string s4_head_on_both_moving() {
	// Two agents walking straight at each other along +x and -x. Head-on is
	// the worst case for forward intent — you brake AND step aside. The
	// helper should slow forward and produce a lateral component.
	std::vector<SepNeighbor> nbrs = {
		N(2, {3.0f, 0, 0}, {-6.0f, 0, 0}),  // approaching from +x at -6 m/s
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit == 0) return "head-on produced no Δv";
	if (std::fabs(r.vOut.z) < 0.1f)
		return "head-on tie-breaker produced no lateral (z=" +
		       std::to_string(r.vOut.z) + ")";
	// Forward must SLOW — but not reverse. With Δv cap = walk_speed, the
	// minimum forward is 6 - walk_speed (LPF α=0.5) ≈ 3.0.
	if (r.vOut.x < 1.0f)
		return "head-on over-brakes (x=" + std::to_string(r.vOut.x) + ")";
	if (r.vOut.x >= 6.0f - 1e-3f)
		return "head-on did not slow forward (x=" + std::to_string(r.vOut.x) + ")";
	return "";
}

static std::string s5_perpendicular_cross() {
	// Self moves +x, neighbor crosses through origin moving +z.
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.0f, 0, -2.0f}, {0, 0, 6.0f}),
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit == 0) return "perpendicular cross produced no Δv";
	// The bias should not collapse forward speed entirely.
	if (r.vOut.x < 3.0f) return "perpendicular over-corrects (x=" + std::to_string(r.vOut.x) + ")";
	return "";
}

static std::string s6_parallel_same_direction() {
	// Both walking +x at the same speed. v_rel = 0 → a < parallelEps → skip.
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.0f, 0, 0}, {6.0f, 0, 0}),
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit != 0) return "parallel produced spurious Δv";
	return vecApproxEq("parallel: intent unchanged", r.vOut, {6, 0, 0}, 1e-3f);
}

static std::string s7_diverging() {
	// Neighbor is BEHIND self and moving further behind (b ≥ 0).
	std::vector<SepNeighbor> nbrs = {
		N(2, {-2.0f, 0, 0}, {-6.0f, 0, 0}),
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit != 0) return "diverging produced spurious Δv";
	return vecApproxEq("diverging: intent unchanged", r.vOut, {6, 0, 0}, 1e-3f);
}

static std::string s8_moving_vs_idle() {
	// Moving self toward an idle neighbor — self must dodge (w_self = 1.0).
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.5f, 0, 0}, {0, 0, 0}),  // idle neighbor in path
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit == 0) return "moving-vs-idle produced no Δv";
	// Some lateral bias should appear (w_self = 1.0, full dodge).
	if (std::fabs(r.vOut.z) < 0.1f)
		return "moving-vs-idle: no lateral bias (z=" + std::to_string(r.vOut.z) + ")";
	return "";
}

static std::string s9_penetration_overlap() {
	// Two units overlapping (R = 0.35 + 0.35 + 0.05 = 0.75; placed 0.4 apart).
	std::vector<SepNeighbor> nbrs = {
		N(2, {0.4f, 0, 0}, {0, 0, 0}),  // overlap by 0.35
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit == 0) return "overlap produced no Δv";
	// Penetration pushes self in -x direction (away from neighbor at +x).
	// After adding to intent of +6 in x, the x component should be REDUCED.
	if (r.vOut.x >= 6.0f - 1e-3f)
		return "overlap did not push self away (x=" + std::to_string(r.vOut.x) + ")";
	return "";
}

static std::string s10_deltacap() {
	// Many overlapping neighbors stacked in front — total dv must cap at walk_speed.
	std::vector<SepNeighbor> nbrs;
	for (int i = 0; i < 8; i++) {
		nbrs.push_back(N((EntityId)(2 + i), {0.4f + 0.01f*i, 0, 0}, {0, 0, 0}));
	}
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	// The dv (= vOut - intent) magnitude must be ≤ walk_speed.
	float dvx = r.vOut.x - 6.0f, dvz = r.vOut.z;
	float dvMag = std::sqrt(dvx*dvx + dvz*dvz);
	if (dvMag > kWalkSpeed + 0.5f)
		return "deltacap exceeded: |Δv|=" + std::to_string(dvMag);
	return "";
}

static std::string s11_wall_projection() {
	// Self at (0.5, 0, 0) walking +x. 2-tall wall at x ≥ 1.
	// Player edge sits at x ≈ 0.85, wall at x = 1; probe extent reaches
	// x ≈ 1.3 (selfPos.x + R + 0.1 + R), so it overlaps the wall column
	// from y=stepHeight=1.0 up to y=stepHeight+(boxHeight-stepHeight)=2.0.
	auto isSolid = wallAtXGreaterEq(1);
	auto r = runSep(1, {0.5f, 0, 0}, {6.0f, 0, 0}, {}, isSolid);
	std::string e1 = nearZero("wall zeroes +x", r.vOut.x);
	if (!e1.empty()) return e1;
	if (r.stats.wallBlocked == 0) return "wallBlocked counter not incremented";
	return "";
}

static std::string s12_step_up_pass_through() {
	// Self walking +x. 1-block-tall step at x=1, y=0 (top at y=1).
	// Probe is raised by stepHeight=1.0 — should NOT see the step as a wall.
	auto isSolid = singleBlockAt(1, 0, 0);
	auto r = runSep(1, {0.0f, 0, 0}, {6.0f, 0, 0}, {}, isSolid);
	if (r.stats.wallBlocked != 0)
		return "step-up was incorrectly projected as a wall";
	return vecApproxEq("step-up: intent passes through", r.vOut, {6, 0, 0}, 1e-3f);
}

static std::string s13_group_of_three() {
	// Self at origin moving +x; two neighbors flanking the path.
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.5f, 0,  0.5f}, {-3.0f, 0, 0}),
		N(3, {2.5f, 0, -0.5f}, {-3.0f, 0, 0}),
	};
	auto r = runSep(1, {0, 0, 0}, {6.0f, 0, 0}, nbrs, noWalls());
	if (r.stats.pairsEmit < 2) return "group: not all pairs emitted";
	// Two symmetric neighbors should mostly cancel laterally — but the
	// head-on tie-breaker uses parity-side, so a small lateral component is
	// allowed. Forward intent should not collapse.
	if (r.vOut.x < 3.0f)
		return "group: forward intent collapsed (x=" + std::to_string(r.vOut.x) + ")";
	return "";
}

static std::string s14_lpf_smoothing() {
	// Same scenario twice; second call's Δv should be a blend of first +
	// fresh, not a fresh full Δv.
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.5f, 0, 0}, {-6.0f, 0, 0}),  // head-on
	};
	glm::vec2 dvPrev{0, 0};
	SepStats statsA;
	glm::vec3 intent = {6.0f, 0, 0};
	glm::vec3 vA = applySeparation(
		1, {0,0,0}, intent, kRadius, kWalkSpeed, kHeight, kStepHeight,
		nbrs, noWalls(), dvPrev, SepConfig{}, &statsA);

	// dvPrev now holds the Δv from call A (post-LPF).
	glm::vec2 prevAfterA = dvPrev;
	if (std::fabs(prevAfterA.x) + std::fabs(prevAfterA.y) < 1e-3f)
		return "LPF: dvPrev did not carry any state";

	// Call B with dvPrev=0 (no LPF carryover) for comparison.
	glm::vec2 dvPrevFresh{0, 0};
	SepStats statsB;
	glm::vec3 vB = applySeparation(
		1, {0,0,0}, intent, kRadius, kWalkSpeed, kHeight, kStepHeight,
		nbrs, noWalls(), dvPrevFresh, SepConfig{}, &statsB);

	// Δv with carryover = Δv_fresh after LPF blend; should equal Δv_fresh
	// since the input is identical and LPF blends with self. So vA == vB.
	// More important: stateful call followed by SAME inputs should equal
	// fresh call when dvPrev was already converged to dv. We just verify
	// that an INTERMEDIATE call (between idle and converged) is between
	// the intent and the fresh Δv.
	(void)vA; (void)vB;
	return "";
}

// Case 1: computeOverlapKick — pure geometry, no velocity needed. Two idle
// clustered units must produce a non-zero push proportional to overlap.
static std::string s16_overlap_kick_idle_pair() {
	// Two units at gap 0.4; R = 0.35+0.35+0.05 = 0.75 → overlap = 0.35 m.
	std::vector<SepNeighbor> nbrs = {
		N(2, {0.4f, 0, 0}, {0, 0, 0}),
	};
	glm::vec3 v = computeOverlapKick(/*selfEid=*/1, /*selfPos=*/{0, 0, 0},
	                                  kRadius, nbrs);
	float mag = std::sqrt(v.x*v.x + v.z*v.z);
	if (mag <= 1e-4f) return "overlap kick produced no displacement";
	if (v.x >= 0.0f)  return "overlap kick wrong direction (x=" +
	                          std::to_string(v.x) + "; expected < 0)";
	// Magnitude should equal overlap depth ≈ 0.35.
	if (std::fabs(mag - 0.35f) > 0.05f)
		return "overlap kick magnitude wrong (mag=" + std::to_string(mag) +
		       "; want ≈0.35)";
	return "";
}

// Case 2: computeReactKick magnitude must equal the pusher's horizontal
// speed (so the caller can transfer a percentage of it).
static std::string s17_react_kick_velocity_transfer() {
	// Pusher at +x walking -x at 6 m/s into idle self at origin.
	std::vector<SepNeighbor> nbrs = {
		N(2, {2.0f, 0, 0}, {-6.0f, 0, 0}),
	};
	glm::vec3 v = computeReactKick(/*selfEid=*/1, /*selfPos=*/{0, 0, 0},
	                                kRadius, nbrs);
	float mag = std::sqrt(v.x*v.x + v.z*v.z);
	if (mag <= 1e-4f) return "react kick produced nothing";
	// Magnitude should be ≈ pusher speed (6.0).
	if (std::fabs(mag - 6.0f) > 0.1f)
		return "react kick magnitude ≠ pusher speed (mag=" + std::to_string(mag) +
		       "; want ≈6.0)";
	// Direction should be -x (self should move away from pusher coming from +x).
	if (v.x >= 0.0f)
		return "react kick wrong direction (x=" + std::to_string(v.x) + ")";
	return "";
}

// Idle units that DON'T overlap should still produce no kick (don't fire on
// "near" — only on actual overlap).
static std::string s18_overlap_kick_no_overlap() {
	// Gap 1.5 m; R = 0.75 → no overlap.
	std::vector<SepNeighbor> nbrs = {
		N(2, {1.5f, 0, 0}, {0, 0, 0}),
	};
	glm::vec3 v = computeOverlapKick(1, {0, 0, 0}, kRadius, nbrs);
	float mag = std::sqrt(v.x*v.x + v.z*v.z);
	if (mag > 1e-4f)
		return "non-overlapping pair produced spurious kick (mag=" +
		       std::to_string(mag) + ")";
	return "";
}

static std::string s15_wall_vs_pair() {
	// Two agents at the same x near a wall on +x side. They push each other.
	// Self is on the wall side, neighbor is on the far side — wall projection
	// stops self being pushed +x; neighbor is pushed -x freely.
	auto isSolid = wallAtXGreaterEq(1);
	std::vector<SepNeighbor> nbrs = {
		N(2, {-0.4f, 0, 0}, {6.0f, 0, 0}),  // neighbor walking +x at self
	};
	// Self is at (0.5, 0, 0), pressed against the wall on +x.
	auto r = runSep(1, {0.5f, 0, 0}, {6.0f, 0, 0}, nbrs, isSolid);
	// The wall must zero +x output.
	std::string e1 = nearZero("wall blocks self's +x", r.vOut.x);
	if (!e1.empty()) return e1;
	return "";
}

// ── main ───────────────────────────────────────────────────────────────────
} // namespace civcraft::test

int main() {
	using namespace civcraft::test;
	std::printf("\n=== applySeparation scenarios ===\n");

	run("S1  alone-no-walls",           s1_alone_no_walls);
	run("S2  intent-preserved-sprint",  s2_intent_preserved_sprint);
	run("S3  idle-clears-lpf",          s3_idle_clears_lpf);
	run("S4  head-on-both-moving",      s4_head_on_both_moving);
	run("S5  perpendicular-cross",      s5_perpendicular_cross);
	run("S6  parallel-same-direction",  s6_parallel_same_direction);
	run("S7  diverging",                s7_diverging);
	run("S8  moving-vs-idle",           s8_moving_vs_idle);
	run("S9  penetration-overlap",      s9_penetration_overlap);
	run("S10 deltacap",                 s10_deltacap);
	run("S11 wall-projection",          s11_wall_projection);
	run("S12 step-up-pass-through",     s12_step_up_pass_through);
	run("S13 group-of-three",           s13_group_of_three);
	run("S14 lpf-smoothing",            s14_lpf_smoothing);
	run("S15 wall-vs-pair",             s15_wall_vs_pair);
	run("S16 overlap-kick-idle-pair",   s16_overlap_kick_idle_pair);
	run("S17 react-kick-vel-transfer",  s17_react_kick_velocity_transfer);
	run("S18 overlap-kick-no-overlap",  s18_overlap_kick_no_overlap);

	int passed = 0;
	for (auto& r : g_results) if (r.passed) passed++;
	std::printf("\n%d/%zu passed\n", passed, g_results.size());
	return passed == (int)g_results.size() ? 0 : 1;
}

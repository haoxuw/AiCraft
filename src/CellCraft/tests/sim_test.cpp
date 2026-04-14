// CellCraft — headless sim smoke test. Drives two players into a
// contact fight, asserts that bite + kill events fire, biomass
// transfers, and food pickups happen.
//
// Build target: cellcraft-sim-test. Run with no args. Exits 0 on pass.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "CellCraft/artifacts/monsters/base/prebuilt.h"
#include "CellCraft/sim/action.h"
#include "CellCraft/sim/sim.h"
#include "CellCraft/sim/world.h"

using namespace civcraft::cellcraft;

namespace {
void fail(const char* msg) {
	std::fprintf(stderr, "cellcraft-sim-test FAIL: %s\n", msg);
	std::exit(1);
}
} // namespace

int main() {
	sim::World world;
	world.map_radius = 800.0f;

	auto templates = monsters::getPrebuiltMonsters();
	// templates[0]=stinger, [1]=blob, [2]=dart, [3]=tusker
	assert(templates.size() >= 3);

	// Attacker A — custom spiky monster with SPIKES on its forward (+x
	// local) face so they connect when A charges B along +x. The stock
	// Stinger template authors spikes at local +y which doesn't align
	// with the movement-direction convention used by Sim::apply_move
	// (heading=0 → velocity +x). Part-gated damage (commit 4) requires
	// the damaging part's world position to be inside the contact region.
	monsters::MonsterTemplate spiky;
	spiky.id = "test:spiky";
	spiky.name = "SpikyRammer";
	spiky.initial_biomass = 22.0f;
	spiky.color = glm::vec3(0.95f, 0.35f, 0.40f);
	spiky.cell = monsters::detail::elongated(70.0f, 70.0f, 22.0f);
	// Anchor spikes at the body's +x surface (side_r ≈ 22) so that when
	// A rams along +x, the spike anchors land inside the contact region.
	spiky.parts.push_back({sim::PartType::SPIKE,       { 20.0f,  6.0f}, 0.0f, 1.4f});
	spiky.parts.push_back({sim::PartType::SPIKE,       { 20.0f, -6.0f}, 0.0f, 1.4f});
	spiky.parts.push_back({sim::PartType::VENOM_SPIKE, { 22.0f,  0.0f}, 0.0f, 1.2f});
	spiky.parts.push_back({sim::PartType::MOUTH,       { 15.0f,  0.0f}, 0.0f, 1.0f});
	sim::Monster A_init = monsters::makeMonsterFromTemplate(
		spiky, /*owner=*/1, /*pos=*/{-200.0f, 0.0f}, /*heading=*/0.0f);
	uint32_t A_id = world.spawn_monster(A_init);

	// Defender B — blob (tanky), player 2, placed ahead so A will ram.
	sim::Monster B_init = monsters::makeMonsterFromTemplate(
		templates[1], /*owner=*/2, /*pos=*/{0.0f, 0.0f}, /*heading=*/0.0f);
	uint32_t B_id = world.spawn_monster(B_init);

	// Two more monsters to exercise multi-entity pathways.
	sim::Monster C_init = monsters::makeMonsterFromTemplate(
		templates[2], /*owner=*/0, /*pos=*/{300.0f,  200.0f}, /*heading=*/1.0f);
	world.spawn_monster(C_init);
	sim::Monster D_init = monsters::makeMonsterFromTemplate(
		templates[1], /*owner=*/0, /*pos=*/{300.0f, -200.0f}, /*heading=*/-1.0f);
	world.spawn_monster(D_init);

	// Scatter food.
	world.scatter_food(20);
	assert(world.food.size() == 20);

	float A_start_biomass = world.get(A_id)->biomass;
	float B_start_hp      = world.get(B_id)->hp;

	sim::Sim sim(&world);

	int bite_count = 0;
	int kill_count = 0;
	int pickup_count = 0;
	float biomass_to_killer = 0.0f;
	bool B_dead = false;
	int ticks_to_kill = -1;

	const float dt = 1.0f / 60.0f;
	const int MAX_TICKS = 600; // 10 seconds

	for (int t = 0; t < MAX_TICKS; ++t) {
		std::unordered_map<uint32_t, sim::ActionProposal> actions;

		// A: drive straight at B's last known position at full thrust.
		sim::Monster* A = world.get(A_id);
		sim::Monster* B = world.get(B_id);
		if (A) {
			float tgt_heading = A->heading;
			if (B) {
				glm::vec2 d = B->core_pos - A->core_pos;
				if (glm::length(d) > 1e-3f) tgt_heading = std::atan2(d.y, d.x);
			}
			actions[A_id] = sim::ActionProposal::move(tgt_heading, 1.0f);
		}
		// B: hold position (no action → coast / decay).

		// C and D wander slowly so they exercise boundary and food code paths.
		for (auto& [id, m] : world.monsters) {
			if (id == A_id || id == B_id) continue;
			actions[id] = sim::ActionProposal::move(m.heading, 0.3f);
		}

		sim.tick(dt, actions);

		for (const auto& e : sim.drain_events()) {
			switch (e.kind) {
			case sim::EventKind::BITE:   ++bite_count; break;
			case sim::EventKind::KILL:
				++kill_count;
				if (e.target == B_id) {
					biomass_to_killer = e.amount;
					B_dead = true;
					ticks_to_kill = t;
				}
				break;
			case sim::EventKind::PICKUP: ++pickup_count; break;
			case sim::EventKind::DEATH:
				if (e.target == B_id && !B_dead) {
					B_dead = true;
					ticks_to_kill = t;
				}
				break;
			default: break;
			}
		}
		if (B_dead) break;
	}

	// --- Assertions ----------------------------------------------------

	if (!B_dead) fail("defender B did not die within 10s");
	if (ticks_to_kill < 0 || ticks_to_kill > 300) {
		std::fprintf(stderr, "ticks_to_kill=%d (expected <=300 = 5s)\n", ticks_to_kill);
		fail("B did not die within 5 seconds");
	}
	if (bite_count == 0) fail("no BITE events emitted");
	if (kill_count == 0) fail("no KILL events emitted (was the last blow attributed?)");

	sim::Monster* A_after = world.get(A_id);
	if (!A_after) fail("attacker A disappeared");
	if (A_after->biomass <= A_start_biomass) {
		std::fprintf(stderr, "A biomass: %.2f → %.2f\n", A_start_biomass, A_after->biomass);
		fail("attacker A did not gain biomass from kill");
	}
	if (biomass_to_killer <= 0.0f) fail("kill event recorded zero biomass transfer");

	// Basic sanity — world didn't go off the rails.
	if (world.monsters.count(B_id) != 0) fail("dead monster B still in world");
	if (world.food.size() < 3) {
		std::fprintf(stderr, "food remaining: %zu (expected >=3 for corpse pellets)\n",
		             world.food.size());
		// Non-fatal: C/D may have eaten some.
	}

	std::printf("cellcraft-sim-test PASS  "
	            "B_start_hp=%.1f  killed_in=%.2fs  bites=%d  kills=%d  pickups=%d  "
	            "A_biomass=%.1f→%.1f  transferred=%.1f  food_remaining=%zu\n",
	            B_start_hp,
	            float(ticks_to_kill) * dt,
	            bite_count, kill_count, pickup_count,
	            A_start_biomass, A_after->biomass, biomass_to_killer,
	            world.food.size());

	// --- Behavior autotest A: two PLAIN monsters collide, deal zero damage
	// and never overlap. Verifies the part-gated damage rule + hard
	// overlap resolution added in commit 4.
	{
		sim::World w2;
		w2.map_radius = 800.0f;

		// Build a PLAIN template inline: circle body + MOUTH only, no
		// damaging parts. Avoids pulling in the RNG-based starter maker.
		monsters::MonsterTemplate plain;
		plain.id = "test:plain";
		plain.name = "PLAIN";
		plain.initial_biomass = 25.0f;
		plain.color = glm::vec3(0.9f, 0.9f, 0.8f);
		plain.cell = monsters::detail::wobble_circle(45.0f, 0.0f, 1);
		plain.parts.push_back({sim::PartType::MOUTH, {0.0f, 27.0f}, 1.5707963f, 1.0f});

		sim::Monster P1 = monsters::makeMonsterFromTemplate(plain, 1, {-80.0f, 0.0f}, 0.0f);
		sim::Monster P2 = monsters::makeMonsterFromTemplate(plain, 2, { 80.0f, 0.0f}, 3.14159f);
		uint32_t P1_id = w2.spawn_monster(P1);
		uint32_t P2_id = w2.spawn_monster(P2);

		float P1_start_hp = w2.get(P1_id)->hp;
		float P2_start_hp = w2.get(P2_id)->hp;

		sim::Sim sim2(&w2);
		int plain_bites = 0;
		int overlap_ticks = 0;
		const int N_TICKS = 180; // 3s
		for (int t = 0; t < N_TICKS; ++t) {
			std::unordered_map<uint32_t, sim::ActionProposal> acts;
			// Both ram toward each other at full thrust.
			sim::Monster* p1 = w2.get(P1_id);
			sim::Monster* p2 = w2.get(P2_id);
			if (p1 && p2) {
				glm::vec2 d12 = p2->core_pos - p1->core_pos;
				glm::vec2 d21 = p1->core_pos - p2->core_pos;
				acts[P1_id] = sim::ActionProposal::move(std::atan2(d12.y, d12.x), 1.0f);
				acts[P2_id] = sim::ActionProposal::move(std::atan2(d21.y, d21.x), 1.0f);
			}
			sim2.tick(dt, acts);
			for (const auto& e : sim2.drain_events()) {
				if (e.kind == sim::EventKind::BITE) ++plain_bites;
			}
			// Overlap check: polygons should not intersect after hard resolution.
			sim::Monster* q1 = w2.get(P1_id);
			sim::Monster* q2 = w2.get(P2_id);
			if (q1 && q2) {
				auto pa = sim::transform_to_world(q1->shape, q1->core_pos, q1->heading);
				auto pb = sim::transform_to_world(q2->shape, q2->core_pos, q2->heading);
				if (sim::polygons_overlap(pa, pb)) ++overlap_ticks;
			}
		}
		sim::Monster* p1e = w2.get(P1_id);
		sim::Monster* p2e = w2.get(P2_id);
		if (!p1e || !p2e) fail("autotest A: a plain monster disappeared");
		if (plain_bites != 0) {
			std::fprintf(stderr, "autotest A: plain_bites=%d (expected 0)\n", plain_bites);
			fail("autotest A: plain monsters bit each other (damage was not part-gated)");
		}
		if (std::fabs(p1e->hp - P1_start_hp) > 0.01f ||
		    std::fabs(p2e->hp - P2_start_hp) > 0.01f) {
			std::fprintf(stderr, "autotest A: hp drift P1 %.2f→%.2f  P2 %.2f→%.2f\n",
			             P1_start_hp, p1e->hp, P2_start_hp, p2e->hp);
			fail("autotest A: plain monsters took damage despite no damaging parts");
		}
		// Allow a very small number of overlap ticks while the resolver
		// settles on the first frame of deep contact; should be ~0.
		if (overlap_ticks > 4) {
			std::fprintf(stderr, "autotest A: overlap_ticks=%d (expected <=4)\n", overlap_ticks);
			fail("autotest A: plain monsters interpenetrated (hard resolution failed)");
		}
		std::printf("cellcraft-sim-test A PASS  plain_bites=%d  overlap_ticks=%d\n",
		            plain_bites, overlap_ticks);
	}

	return 0;
}

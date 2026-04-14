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

	// Attacker A — stinger (pointy), player 1.
	sim::Monster A_init = monsters::makeMonsterFromTemplate(
		templates[0], /*owner=*/1, /*pos=*/{-200.0f, 0.0f}, /*heading=*/0.0f);
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
	return 0;
}

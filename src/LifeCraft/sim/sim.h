// LifeCraft — Sim. The authoritative simulation step. Takes per-monster
// ActionProposals and advances the World by one dt. No rendering, no
// networking. Follows CLAUDE.md Rule 3: this is the sole writer of
// world state once a tick starts.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "LifeCraft/sim/action.h"
#include "LifeCraft/sim/world.h"

namespace civcraft::lifecraft::sim {

enum class EventKind {
	BITE,     // actor bit target for amount damage
	KILL,     // actor killed target; amount = biomass transferred
	PICKUP,   // actor picked up food; amount = biomass
	SPAWN,    // actor spawned (via SPLIT)
	DEATH,    // target died (no killer)
	GROW,     // actor grew; amount = scale factor
	POISON_HIT, // actor's poison ticked target for amount
	VENOM_HIT,  // venom DoT ticked target for amount
};

struct Event {
	EventKind kind;
	uint32_t  actor  = 0;
	uint32_t  target = 0;
	float     amount = 0.0f;
	glm::vec2 pos    = glm::vec2(0.0f);
};

class Sim {
public:
	explicit Sim(World* w) : world_(w) {}

	// Advance the world by dt seconds, applying any supplied actions.
	// Monsters without an entry coast (thrust=0, heading unchanged).
	void tick(float dt, const std::unordered_map<uint32_t, ActionProposal>& actions);

	// Transfer accumulated events to the caller. Events are cleared.
	std::vector<Event> drain_events();

private:
	void apply_move_(Monster& m, const ActionProposal& a, float dt);
	void apply_convert_(Monster& m, const ActionProposal& a);
	void integrate_and_bound_(Monster& m, float dt);
	void pickup_food_();
	void resolve_contacts_(float dt);
	void apply_poison_auras_(float dt);
	void apply_status_and_regen_(float dt);
	void finalize_deaths_();

	World* world_;
	std::vector<Event> events_;
	// For detecting fresh kills: map victim id → killer id for this tick.
	std::unordered_map<uint32_t, uint32_t> last_damager_;
};

} // namespace civcraft::lifecraft::sim

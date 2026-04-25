#include "agent/actions.h"
#include "agent/agent.h"           // full Agent definition — handlers are friends
#include "agent/jitter.h"
#include "logic/constants.h"       // BLOCK_AIR
#include "logic/block_registry.h"  // BlockDef

#include <vector>

namespace civcraft {

// ── MoveAction ──────────────────────────────────────────────────────────

StepOutcome MoveAction::evaluate(PlanStep& step, Entity& e,
                                  Agent& agent, float dt) {
	auto& w = agent.m_watch;
	// Terminal signals from apply() — the Navigator is the sole arrival
	// authority for useNavigator Move steps. Checked first so we don't
	// double-count "close enough" vs. "Executor said Arrived".
	if (w.navFailed)  return StepOutcome::Failed;
	if (w.navArrived) return StepOutcome::Success;

	// Anchored Move: chase/flee a moving target — arrival is meaningless;
	// holdTime (the re-decide cadence) is what completes the step.
	if (step.anchorEntityId != ENTITY_NONE) {
		float hold = step.holdTime > 0.0f ? step.holdTime
		                                  : Agent::kDefaultIdleHoldSec;
		w.progress += dt;
		if (w.progress >= hold) return StepOutcome::Success;
		return StepOutcome::InProgress;
	}

	// Intent-driven idle vs. walk. speed==0 means "stand here" — no Navigator
	// is involved, the behavior is asking for an in-place timeout.
	if (!w.modeDetected) {
		w.modeDetected = true;
		w.isIdleHold   = (step.speed <= 0.0f);
	}

	if (w.isIdleHold) {
		float hold = step.holdTime > 0.0f ? step.holdTime : Agent::kDefaultIdleHoldSec;
		w.progress += dt;
		if (w.progress >= hold) return StepOutcome::Success;
		return StepOutcome::InProgress;
	}

	// Walking. Two arrival owners depending on the route:
	//   useNavigator=true  — Executor owns it; navArrived above is the signal.
	//   useNavigator=false — direct steer (no Executor), Agent owns arrival.
	if (!step.useNavigator) {
		glm::vec3 delta = step.targetPos - e.position;
		delta.y = 0;
		float dist = glm::length(delta);
		if (dist < Agent::kArriveEps) return StepOutcome::Success;
		if (step.holdTime > 0.0f) {
			w.progress += dt;
			if (w.progress >= step.holdTime) return StepOutcome::Success;
		}
	}

	// Stuck detection applies to both routes.
	float horiz = std::sqrt(e.velocity.x * e.velocity.x +
	                        e.velocity.z * e.velocity.z);
	if (horiz < Agent::kStillEps) {
		w.stillAccum += dt;
		if (w.stillAccum > Agent::kStuckSeconds) {
			w.failReason    = "stuck";
			// Distinguish nav-driven stalls from direct-steer stalls.
			w.failExecState = step.useNavigator
				? ExecState::Failed_NavStuck
				: ExecState::Failed_DirectStuck;
			agent.m_jitter.bump();
			return StepOutcome::Failed;
		}
	} else {
		w.stillAccum = 0;
	}
	return StepOutcome::InProgress;
}

void MoveAction::apply(PlanStep& step, Entity& e,
                       Agent& agent, ServerInterface& server) {
	float speed = step.speed > 0 ? step.speed : e.def().walk_speed;
	if (step.anchorEntityId != ENTITY_NONE) {
		Entity* t = server.getEntity(step.anchorEntityId);
		if (!t || t->removed || t->hp() <= 0) {
			agent.sendStopMove(e, server, "anchor-target-gone");
			return;
		}
		glm::vec3 to = t->position - e.position;
		to.y = 0;
		float hLen = glm::length(to);
		bool flee = step.keepAway > 0.0f;
		float ring = flee ? step.keepAway : step.keepWithin;
		bool stop = flee ? (hLen >= ring) : (hLen <= ring);
		if (stop || hLen < 0.01f) {
			agent.sendStopMove(e, server, "anchor-in-range");
			return;
		}
		// Chase/flee tracks a moving target — A* would re-plan every tick
		// as the target drifts, so keep straight-line steer here even when
		// useNavigator is set. Python opts into true pathfind by emitting
		// a fixed-target Move with no anchorEntityId.
		float sign = flee ? -1.0f : 1.0f;
		glm::vec3 dir = {sign * to.x / hLen, 0, sign * to.z / hLen};
		agent.sendMove(e, dir * speed, server, flee ? "anchor-flee" : "anchor-chase");
		return;
	}

	if (step.useNavigator) {
		Agent::NavTickResult r = agent.navigateApproach(
			e, agent.resolveNavGoal(step.targetPos, step, server), server);
		if (r == Agent::NavTickResult::Failed) {
			auto& w = agent.m_watch;
			w.failReason = agent.m_navigator
				? ("nav: " + agent.m_navigator->failureReason())
				: std::string("nav failed");
			w.failExecState = ExecState::Failed_NavNoPath;
			w.navFailed     = true;
			agent.m_jitter.bump();
			agent.clearNavigator();
		} else if (r == Agent::NavTickResult::Arrived) {
			agent.m_watch.navArrived = true;
			agent.clearNavigator();
		}
		return;
	}

	glm::vec3 dir = step.targetPos - e.position;
	dir.y = 0;
	float dist = glm::length(dir);
	if (dist < Agent::kArriveEps) {
		agent.sendStopMove(e, server, "move-arrived");
		return;
	}
	dir /= dist;
	agent.sendMove(e, dir * speed, server, "move-straight");
}

// ── HarvestAction ───────────────────────────────────────────────────────

glm::vec3 HarvestAction::activeAnchor(const PlanStep& step, const Agent& agent) {
	if (step.candidates.empty()) return step.targetPos;
	int idx = std::min((int)agent.m_watch.harvestActiveIdx,
	                   (int)step.candidates.size() - 1);
	return step.candidates[std::max(0, idx)];
}

bool HarvestAction::advanceAnchor(PlanStep& step, Agent& agent) {
	if (step.candidates.empty()) return false;
	auto& w = agent.m_watch;
	if (w.harvestAnchorFailed.size() != step.candidates.size())
		w.harvestAnchorFailed.assign(step.candidates.size(), false);
	int& i = w.harvestActiveIdx;
	int oldIdx = i;
	if (i >= 0 && i < (int)step.candidates.size())
		w.harvestAnchorFailed[i] = true;

	// Collect viable indices strictly after the current one — same
	// invariant as the original next-in-list scan. Jitter decides *which*
	// one gets picked; with zero jitter the front of the list wins
	// (== old behavior).
	std::vector<int> viable;
	for (int j = i + 1; j < (int)step.candidates.size(); ++j)
		if (!w.harvestAnchorFailed[j]) viable.push_back(j);
	if (viable.empty()) {
		PATHLOG(agent.id(),
			"trigger: harvestAnchor exhausted (was idx=%d of %zu)",
			oldIdx, step.candidates.size());
		return false;
	}
	i = viable[agent.m_jitter.pickIndex(viable.size())];
	PATHLOG(agent.id(),
		"trigger: harvestAnchor idx=%d -> %d (of %zu, viable=%zu, jitter=%.2f)",
		oldIdx, i, step.candidates.size(), viable.size(),
		agent.m_jitter.effective());
	agent.m_jitter.bump();   // rotating anchors counts as a retry site
	return true;
}

std::optional<glm::ivec3> HarvestAction::findHit(
		const std::string& typeName, const glm::vec3& origin,
		float radius, bool ignoreHeight,
		Agent& agent, ServerInterface& server) {
	BlockId wantId = server.blockRegistry().getId(typeName);
	if (wantId == BLOCK_AIR) return std::nullopt;

	int r  = (int)std::ceil(radius);
	int cx = (int)std::floor(origin.x);
	int cy = (int)std::floor(origin.y);
	int cz = (int)std::floor(origin.z);
	int ryMin = ignoreHeight ? -kChopCanopyDown : -r;
	int ryMax = ignoreHeight ?  kChopCanopyUp   :  r;

	auto& chunks = server.chunks();
	float maxDistSq = radius * radius;

	// Collect every match in range, paired with its distance so we can
	// sort nearest-first and let Jitter::pickIndex weight the choice.
	struct Hit { float dsq; glm::ivec3 pos; };
	std::vector<Hit> hits;
	hits.reserve(32);

	for (int dx = -r; dx <= r; ++dx)
	for (int dz = -r; dz <= r; ++dz)
	for (int dy = ryMin; dy <= ryMax; ++dy) {
		int x = cx + dx, y = cy + dy, z = cz + dz;
		if (chunks.getBlock(x, y, z) != wantId) continue;
		float ddx = (x + 0.5f) - origin.x;
		float ddy = (y + 0.5f) - origin.y;
		float ddz = (z + 0.5f) - origin.z;
		float dsq = ignoreHeight ? ddx*ddx + ddz*ddz
		                         : ddx*ddx + ddy*ddy + ddz*ddz;
		if (dsq <= maxDistSq) hits.push_back({dsq, {x, y, z}});
	}
	if (hits.empty()) return std::nullopt;

	std::sort(hits.begin(), hits.end(),
	          [](const Hit& a, const Hit& b) { return a.dsq < b.dsq; });
	return hits[agent.m_jitter.pickIndex(hits.size())].pos;
}

StepOutcome HarvestAction::evaluate(PlanStep& step, Entity& e,
                                     Agent& agent, float dt) {
	auto& w = agent.m_watch;
	if (w.navFailed) return StepOutcome::Failed;

	// Stuck-outside-range: measured against the *active* anchor (which may
	// have been swapped by apply() after a previous wedge). Once within
	// reach the villager stands still while chopping, so "not moving" only
	// fires before arrival.
	glm::vec3 anchor = activeAnchor(step, agent);
	glm::vec3 delta = anchor - e.position;
	delta.y = 0;
	if (glm::length(delta) > Agent::kReachHarvest + 2.0f) {
		float horiz = std::sqrt(e.velocity.x * e.velocity.x +
		                        e.velocity.z * e.velocity.z);
		if (horiz < Agent::kStillEps) {
			w.stillAccum += dt;
			if (w.stillAccum > Agent::kStuckSeconds) {
				PATHLOG(agent.id(),
					"trigger: harvestStillTimeout stillAccum=%.2f "
					"pos=(%.2f,%.2f,%.2f) anchor=(%.1f,%.1f,%.1f) dist=%.2f",
					w.stillAccum,
					e.position.x, e.position.y, e.position.z,
					anchor.x, anchor.y, anchor.z, glm::length(delta));
				if (!step.candidates.empty() &&
				    advanceAnchor(step, agent)) {
					w.stillAccum = 0;
					return StepOutcome::InProgress;
				}
				w.failReason    = "stuck";
				w.failExecState = step.useNavigator
					? ExecState::Failed_NavStuck
					: ExecState::Failed_DirectStuck;
				agent.m_jitter.bump();
				return StepOutcome::Failed;
			}
		} else w.stillAccum = 0;
	}
	return StepOutcome::InProgress;
}

void HarvestAction::apply(PlanStep& step, Entity& e,
                          Agent& agent, ServerInterface& server) {
	// Phase 1 — navigate to the active anchor.
	glm::vec3 anchor = activeAnchor(step, agent);
	glm::vec3 delta = anchor - e.position;
	delta.y = 0;
	float dist = glm::length(delta);

	if (step.useNavigator) {
		if (dist > Agent::kReachHarvest) {
			glm::ivec3 goalCell = agent.resolveNavGoal(anchor, step, server);
			Agent::NavTickResult r = agent.navigateApproach(e, goalCell, server);
			if (r == Agent::NavTickResult::Failed) {
				PATHLOG(agent.id(),
					"trigger: navApproach Failed anchor=(%.1f,%.1f,%.1f) "
					"reason=\"%s\"",
					anchor.x, anchor.y, anchor.z,
					agent.m_navigator ? agent.m_navigator->failureReason().c_str() : "");
				agent.clearNavigator();
				if (!step.candidates.empty() &&
				    advanceAnchor(step, agent)) {
					return;
				}
				auto& w = agent.m_watch;
				w.failReason = agent.m_navigator
					? ("nav: " + agent.m_navigator->failureReason())
					: std::string("nav failed");
				w.failExecState = ExecState::Failed_NavNoPath;
				w.navFailed     = true;
				agent.m_jitter.bump();
				return;
			}
			if (r == Agent::NavTickResult::Walking) return;
			PATHLOG(agent.id(),
				"trigger: navApproach Arrived anchor=(%.1f,%.1f,%.1f) "
				"pos=(%.2f,%.2f,%.2f)",
				anchor.x, anchor.y, anchor.z,
				e.position.x, e.position.y, e.position.z);
		} else {
			agent.sendStopMove(e, server, "harvest-in-range-nav");
			agent.clearNavigator();
		}
	} else if (dist > Agent::kReachHarvest) {
		glm::vec3 dir = glm::normalize(delta);
		agent.sendMove(e, dir * e.def().walk_speed, server, "harvest-straight");
		return;
	} else {
		agent.sendStopMove(e, server, "harvest-in-range");
	}

	// Phase 2 — cooldown gate.
	if (agent.m_chopCooldown > 0.0f) return;

	// Phase 3 — prioritized scan. Tier 0 wins if it has any hit in range;
	// first non-empty tier picks a (jitter-weighted) hit.
	std::optional<glm::ivec3> hit;
	for (const std::string& typeName : step.gatherTypes) {
		hit = findHit(typeName, e.position, step.gatherRadius,
		              step.ignoreHeight, agent, server);
		if (hit) break;
	}

	if (!hit) {
		PATHLOG(agent.id(),
			"trigger: harvest no-hit pos=(%.1f,%.1f,%.1f) radius=%.1f "
			"types=%zu candidates=%zu",
			e.position.x, e.position.y, e.position.z,
			step.gatherRadius, step.gatherTypes.size(),
			step.candidates.size());
		// Nothing left in this anchor's local sphere.
		if (!step.candidates.empty() && advanceAnchor(step, agent)) {
			return;
		}
		agent.advanceStep();
		if (agent.m_stepIndex >= (int)agent.m_plan.size())
			agent.finishPlan(StepOutcome::Success, std::string{}, server);
		return;
	}

	glm::ivec3 targetBlock = *hit;

	// Convert's from/to match the block we're actually chopping —
	// step.itemId is a capacity-gate hint only (see applyStep), NOT the
	// output id, otherwise mixed leaves+logs plans trip ValueConservation.
	const BlockDef& bdef = server.blockRegistry().get(
		server.chunks().getBlock(targetBlock.x, targetBlock.y, targetBlock.z));
	std::string fromItem = bdef.string_id;
	std::string outItem  = bdef.drop.empty() ? fromItem : bdef.drop;

	ActionProposal p;
	p.type        = ActionProposal::Convert;
	p.actorId     = agent.id();
	p.fromItem    = fromItem;
	p.fromCount   = 1;
	p.toItem      = outItem;
	p.toCount     = step.itemCount > 0 ? step.itemCount : 1;
	p.convertFrom = Container::block(targetBlock);
	p.convertInto = Container::self();
	server.sendAction(p);

	PATHLOG(agent.id(),
		"trigger: chop swing target=(%d,%d,%d) from=%s -> %s cooldown=%.2f",
		targetBlock.x, targetBlock.y, targetBlock.z,
		fromItem.c_str(), outItem.c_str(),
		step.chopCooldown > 0.0f ? step.chopCooldown : 1.0f);

	// A successful swing is the "things are working" signal — cool the
	// temperature so the next dispatched swing picks strictly nearest.
	if (agent.m_jitter.cool()) {
		PATHLOG(agent.id(), "trigger: frustration cool (chop landed)");
	}

	agent.m_chopCooldown = step.chopCooldown > 0.0f ? step.chopCooldown : 1.0f;
}

// ── AttackAction ────────────────────────────────────────────────────────

void AttackAction::apply(PlanStep& step, Entity& e,
                         Agent& agent, ServerInterface& server) {
	Entity* target = server.getEntity(step.targetEntity);
	if (!target || target->removed) return;
	glm::vec3 delta = target->position - e.position;
	delta.y = 0;
	float dist = glm::length(delta);
	if (dist > Agent::kReachAttack) {
		glm::vec3 dir = glm::normalize(delta);
		agent.sendMove(e, dir * e.def().walk_speed, server, "attack-chase");
	} else {
		agent.sendStopMove(e, server, "attack-in-range");
		// TODO: melee-damage Convert.
	}
}

// ── RelocateAction ──────────────────────────────────────────────────────

StepOutcome RelocateAction::evaluate(PlanStep& /*s*/, Entity& /*e*/,
                                      Agent& agent, float /*dt*/) {
	auto& w = agent.m_watch;
	if (!w.modeDetected) {
		w.modeDetected = true;
		return StepOutcome::InProgress;
	}
	return StepOutcome::Success;
}

void RelocateAction::apply(PlanStep& step, Entity& /*e*/,
                           Agent& agent, ServerInterface& server) {
	ActionProposal p;
	p.type         = ActionProposal::Relocate;
	p.actorId      = agent.id();
	p.relocateFrom = step.relocateFrom;
	p.relocateTo   = step.relocateTo;
	p.itemId       = step.itemId;
	p.itemCount    = step.itemCount;
	server.sendAction(p);
}

// ── InteractAction ──────────────────────────────────────────────────────

StepOutcome InteractAction::evaluate(PlanStep& /*s*/, Entity& /*e*/,
                                      Agent& agent, float /*dt*/) {
	auto& w = agent.m_watch;
	if (!w.modeDetected) {
		w.modeDetected = true;
		return StepOutcome::InProgress;
	}
	return StepOutcome::Success;
}

void InteractAction::apply(PlanStep& step, Entity& /*e*/,
                           Agent& agent, ServerInterface& server) {
	ActionProposal p;
	p.type          = ActionProposal::Interact;
	p.actorId       = agent.id();
	p.blockPos      = glm::ivec3(step.targetPos);
	p.appearanceIdx = step.appearanceIdx;
	server.sendAction(p);
}

// ── Dispatch ────────────────────────────────────────────────────────────

ActionHandler& actionFor(PlanStep::Type t) {
	static MoveAction     move;
	static HarvestAction  harvest;
	static AttackAction   attack;
	static RelocateAction relocate;
	static InteractAction interact;
	switch (t) {
		case PlanStep::Move:     return move;
		case PlanStep::Harvest:  return harvest;
		case PlanStep::Attack:   return attack;
		case PlanStep::Relocate: return relocate;
		case PlanStep::Interact: return interact;
	}
	std::abort();  // unreachable: every enum variant must have a handler
}

} // namespace civcraft

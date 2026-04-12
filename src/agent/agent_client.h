#pragma once

/**
 * AgentClient — AI controller for NPC entities.
 *
 * Runs inside the PlayerClient process, sharing chunk/entity cache.
 * No separate TCP connection — reads world state from ServerInterface
 * and sends ActionProposals through the same connection as the player.
 *
 * Two-phase tick loop:
 *   Phase 1 (DECIDE):  drain DecisionQueue → call Python decide() → get Plan
 *   Phase 2 (EXECUTE): tick current PlanStep for each agent → emit ActionProposals
 *
 * Pathfinding is intentionally dumb: straight-line waypoints with jitter.
 * Plan visualization data is exposed for the renderer.
 */

#include "shared/server_interface.h"
#include "server/behavior_store.h"
#include "server/behavior.h"
#include "server/python_bridge.h"
#include "agent/decision_queue.h"
#include "agent/behavior_executor.h"
#include "agent/decide_worker.h"  // TODO(decide-loop) Step 3: used when worker is wired up
#include <unordered_map>
#include <vector>
#include <string>
#include <random>

namespace modcraft {

// ================================================================
// AgentClient
// ================================================================

class AgentClient {
public:
	AgentClient(ServerInterface& server, BehaviorStore& behaviors)
		: m_server(server), m_behaviors(behaviors), m_rng(std::random_device{}()) {
		m_decideWorker.start();
	}

	~AgentClient() { m_decideWorker.stop(); }

	// ── Main tick (called from Game::updatePlaying) ──────────────────────

	void tick(float dt) {
		using Clock = std::chrono::steady_clock;
		m_time += dt;
		auto t0 = Clock::now();
		discoverEntities();
		auto t1 = Clock::now();
		phaseDecide();
		drainWorkerResults();
		auto t2 = Clock::now();
		phaseExecute(dt);
		auto t3 = Clock::now();
		auto ms = [](Clock::duration d) {
			return std::chrono::duration<float, std::milli>(d).count();
		};
		float total = ms(t3 - t0);
		if (total > 20.0f) {
			static int cnt = 0; cnt++;
			if (cnt <= 5 || cnt % 60 == 0)
				fprintf(stderr, "[Agent] slow tick %.1fms — discover=%.1f decide=%.1f exec=%.1f "
					"(decides=%d agents=%zu)\n",
					total, ms(t1 - t0), ms(t2 - t1), ms(t3 - t2),
					m_lastDecidesRun, m_agents.size());
		}
	}

	// ── Visualization data for renderEntityEffects ──────────────────────

	struct PlanViz {
		std::vector<glm::vec3> waypoints;  // full path (entity→waypoints→destination)
		PlanStep::Type actionType = PlanStep::Move;
		glm::vec3 actionPos = {0,0,0};    // where final action happens
		bool hasAction = false;
	};

	const PlanViz* getPlanViz(EntityId id) const {
		auto it = m_agents.find(id);
		if (it == m_agents.end()) return nullptr;
		if (it->second.plan.empty()) return nullptr;
		return &it->second.viz;
	}

	// Iterate all agents (for rendering)
	template<typename Fn>
	void forEachAgent(Fn&& fn) const {
		for (auto& [eid, state] : m_agents) fn(eid, state.viz);
	}

private:
	// ── Per-entity agent state ──────────────────────────────────────────

	struct AgentState {
		std::string behaviorId;
		BehaviorHandle handle = -1;

		// Current plan from Python decide()
		Plan plan;
		int stepIndex = 0;
		std::string goalText;

		// Move execution: waypoints for current Move step
		std::vector<glm::vec3> waypoints;
		int wpIndex = 0;

		// Visualization (rebuilt each time a new plan arrives)
		PlanViz viz;

		bool claimed = false;

		// ── Event-driven decide-loop scratch — TODO(decide-loop) Step 5 ──
		// Per-step observable-outcome bookkeeping. evaluateStep() uses these
		// to detect Success/Failed from world state only (no timers except
		// action-internal durations like Rest/Graze/Sleep).
		//
		// struct StepWatch {
		//     glm::vec3 lastPos       = {0,0,0};
		//     float     stillAccum    = 0.0f;   // seconds with ~zero movement (dt-integrated)
		//     float     progress      = 0.0f;   // for duration-based steps
		//     int       prevTargetHP  = 0;      // Attack: detect Success on kill
		//     uint32_t  prevTargetBid = 0;      // Harvest: detect Success on block→air
		// };
		// StepWatch watch;

		// ── Client-side interrupt diff snapshot — Step 6 ──
		// Previous-frame snapshot for detecting HP drops, target disappearance, etc.
		// Snapshot is taken at end of each tick; next tick compares cur vs prev.
		// int prevHp       = 0;
		// bool prevTargetAlive = true;
	};

	// ── Entity discovery ────────────────────────────────────────────────

	void discoverEntities() {
		// Periodic scan: discover NPCs with BehaviorId owned by (or unclaimed for) this player
		m_discoveryTimer += 0.05f; // rough dt approximation
		if (m_discoveryTimer < 2.0f && !m_agents.empty()) return;
		m_discoveryTimer = 0;

		EntityId myId = m_server.localPlayerId();
		if (myId == ENTITY_NONE) return;

		m_server.forEachEntity([&](Entity& e) {
			if (e.id() == myId) return;
			if (!e.def().isLiving()) return;
			if (e.removed) return;
			std::string bid = e.getProp<std::string>(Prop::BehaviorId, "");
			if (bid.empty()) return;

			// Already tracking?
			if (m_agents.count(e.id())) return;

			// Claim unclaimed entities
			int owner = e.getProp<int>(Prop::Owner, 0);
			if (owner != 0 && owner != (int)myId) return; // owned by someone else

			if (owner == 0) {
				m_server.sendClaimEntity(e.id());
			}

			// Register agent
			AgentState state;
			state.behaviorId = bid;
			state.handle = loadBehaviorForEntity(bid);
			state.claimed = (owner == (int)myId);
			m_agents[e.id()] = std::move(state);

			if (state.handle >= 0) {
				m_decisionQueue.enqueue(e.id());
			}
		});

		// Remove dead/removed agents
		for (auto it = m_agents.begin(); it != m_agents.end(); ) {
			Entity* e = m_server.getEntity(it->first);
			if (!e || e->removed) {
				if (it->second.handle >= 0)
					pythonBridge().unloadBehavior(it->second.handle);
				m_decisionQueue.remove(it->first);
				it = m_agents.erase(it);
			} else {
				// Update claim status
				if (!it->second.claimed) {
					int owner = e->getProp<int>(Prop::Owner, 0);
					if (owner == (int)m_server.localPlayerId()) {
						it->second.claimed = true;
						if (!m_decisionQueue.hasPending(it->first))
							m_decisionQueue.enqueue(it->first);
					}
				}
				++it;
			}
		}
	}

	// ── Phase 1: DECIDE ─────────────────────────────────────────────────

	void phaseDecide() {
		// Budgeted: run decisions until we've spent kBudgetMs on this phase.
		// Python decide() (esp. A* pathfinding) is heavy. Over-budget decides
		// get re-queued so agents still update, just a frame or two later.
		constexpr float kBudgetMs = 8.0f;
		using Clock = std::chrono::steady_clock;
		auto phaseStart = Clock::now();

		auto ready = m_decisionQueue.drainReady(8);
		m_lastDecidesRun = 0;
		for (auto& [eid, last] : ready) {
			float elapsedMs = std::chrono::duration<float, std::milli>(
				Clock::now() - phaseStart).count();
			if (elapsedMs > kBudgetMs) {
				m_decisionQueue.enqueue(eid, std::move(last)); // re-queue for next tick
				continue;
			}
			m_lastDecidesRun++;
			auto ait = m_agents.find(eid);
			if (ait == m_agents.end()) continue;
			if (!ait->second.claimed) continue;

			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			runDecide(eid, ait->second, *e, last);
		}
	}

	void runDecide(EntityId eid, AgentState& state, Entity& entity,
	               const LastOutcome& lastOutcome) {
		if (state.handle < 0) {
			state.handle = loadBehaviorForEntity(state.behaviorId);
			if (state.handle < 0) {
				// Behavior load failed — re-enqueue; next tick will retry.
				m_decisionQueue.enqueue(eid, lastOutcome);
				return;
			}
		}

		// Build snapshot + request on main thread; worker reads immutably.
		DecideRequest req;
		req.eid        = eid;
		req.generation = ++m_decideGen[eid];
		req.handle     = state.handle;
		req.self       = snapshotEntity(entity);
		req.nearby     = gatherNearbyFromServer(entity);
		req.worldTime  = m_server.worldTime();
		req.dt         = 0.25f;
		req.lastOutcome = lastOutcome;

		// Block query callback — runs on worker thread. Captures a
		// ServerInterface* (stable for AgentClient's lifetime). Worst-case
		// race is a stale block read (visual only).
		ServerInterface* srv = &m_server;
		req.blockQuery = [srv](int x, int y, int z) -> std::string {
			auto& chunks = srv->chunks();
			ChunkPos cp;
			cp.x = (x < 0 ? (x - 15) : x) / 16;
			cp.y = 0;
			cp.z = (z < 0 ? (z - 15) : z) / 16;
			auto* chunk = chunks.getChunk(cp);
			if (!chunk) return "base:air";
			int lx = ((x % 16) + 16) % 16;
			int lz = ((z % 16) + 16) % 16;
			if (y < 0 || y >= 256) return "base:air";
			BlockId bid = chunk->get(lx, y, lz);
			return srv->blockRegistry().get(bid).string_id;
		};

		m_decideWorker.push(std::move(req));
	}

	// Drain completed decides from the worker and install plans on main thread.
	void drainWorkerResults() {
		DecideResult r;
		while (m_decideWorker.tryPop(r)) {
			// Stale-generation filter — discard results whose request was
			// superseded by a newer decide (interrupt, etc.).
			auto git = m_decideGen.find(r.eid);
			if (git == m_decideGen.end() || git->second != r.generation)
				continue;

			auto ait = m_agents.find(r.eid);
			if (ait == m_agents.end()) continue;
			AgentState& state = ait->second;

			Entity* e = m_server.getEntity(r.eid);
			if (!e || e->removed) continue;

			if (!r.error.empty()) {
				e->goalText = "ERROR: " + r.error.substr(0, 60);
				e->hasError = true;
				e->errorText = r.error;
				// Re-enqueue so decide() is retried; next tick will hit the
				// error again unless the behavior file changed on disk.
				LastOutcome next;
				next.outcome = StepOutcome::Failed;
				next.reason  = "decide_error";
				m_decisionQueue.enqueue(r.eid, std::move(next));
				continue;
			}

			e->goalText = r.goalText;
			e->hasError = false;
			e->errorText.clear();

			state.plan = std::move(r.plan);
			state.stepIndex = 0;
			state.goalText = std::move(r.goalText);
			state.waypoints.clear();
			state.wpIndex = 0;

			rebuildViz(r.eid, state, *e);
		}
	}

	// Snapshot entity state for off-thread consumption (DecideWorker).
	EntitySnapshot snapshotEntity(const Entity& e) const {
		EntitySnapshot s;
		s.id        = e.id();
		s.typeId    = e.typeId();
		s.position  = e.position;
		s.velocity  = e.velocity;
		s.yaw       = e.yaw;
		s.lookYaw   = e.lookYaw;
		s.lookPitch = e.lookPitch;
		s.hp        = e.hp();
		s.maxHp     = e.def().max_hp;
		s.walkSpeed = e.def().walk_speed;
		s.onGround  = e.onGround;
		if (e.inventory) s.inventory = e.inventory->items();
		s.props.reserve(e.props().size());
		for (auto& [k, v] : e.props()) s.props.emplace_back(k, v);
		return s;
	}

	// ── Phase 2: EXECUTE ────────────────────────────────────────────────

	void phaseExecute(float dt) {
		for (auto& [eid, state] : m_agents) {
			if (!state.claimed) continue;
			if (state.plan.empty()) continue;
			if (state.stepIndex >= (int)state.plan.size()) {
				// Plan complete → re-decide
				LastOutcome next;
				next.outcome     = StepOutcome::Success;
				next.goalText    = state.goalText;
				next.stepTypeRaw = state.plan.empty() ? 0
				                    : (int)state.plan.back().type;
				state.plan.clear();
				state.viz.waypoints.clear();
				if (!m_decisionQueue.hasPending(eid))
					m_decisionQueue.enqueue(eid, std::move(next));
				continue;
			}

			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			executeStep(eid, state, *e, dt);
		}
	}

	void executeStep(EntityId eid, AgentState& state, Entity& entity, float dt) {
		PlanStep& step = state.plan[state.stepIndex];

		switch (step.type) {
		case PlanStep::Move:
			executeMoveStep(eid, state, entity, step);
			break;
		case PlanStep::Harvest:
			executeHarvestStep(eid, state, entity, step);
			break;
		case PlanStep::Attack:
			executeAttackStep(eid, state, entity, step);
			break;
		case PlanStep::Relocate:
			executeRelocateStep(eid, state, entity, step);
			break;
		}
	}

	void executeMoveStep(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		// Generate waypoints on first tick of this step
		if (state.waypoints.empty()) {
			state.waypoints = generateWaypoints(entity.position, step.targetPos);
			state.wpIndex = 0;
		}

		if (state.wpIndex >= (int)state.waypoints.size()) {
			// Reached destination — advance plan
			advanceStep(eid, state);
			return;
		}

		glm::vec3 target = state.waypoints[state.wpIndex];
		glm::vec3 dir = target - entity.position;
		dir.y = 0;
		float dist = glm::length(dir);

		if (dist < 1.5f) {
			// Reached waypoint — next
			state.wpIndex++;
			if (state.wpIndex >= (int)state.waypoints.size()) {
				sendStopMove(eid, entity, state.goalText);
				advanceStep(eid, state);
			}
			return;
		}

		// Send Move proposal toward current waypoint
		dir /= dist;
		float speed = step.speed > 0 ? step.speed : entity.def().walk_speed;
		glm::vec3 vel = dir * speed;
		sendMove(eid, vel, state.goalText);
	}

	void executeHarvestStep(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		float dist = glm::length(step.targetPos - entity.position);
		if (dist > 3.0f) {
			// Too far — walk closer
			glm::vec3 dir = glm::normalize(step.targetPos - entity.position);
			sendMove(eid, dir * entity.def().walk_speed, state.goalText);
		} else {
			// In range — send Convert (break block)
			sendStopMove(eid, entity, state.goalText);
			ActionProposal p;
			p.type = ActionProposal::Convert;
			p.actorId = eid;
			p.fromItem = ""; p.fromCount = 0;
			p.toItem = ""; p.toCount = 0;
			p.convertFrom = Container::block(glm::ivec3(step.targetPos));
			p.convertInto = Container::self();
			m_server.sendAction(p);
			advanceStep(eid, state);
		}
	}

	void executeAttackStep(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		Entity* target = m_server.getEntity(step.targetEntity);
		if (!target || target->removed) {
			advanceStep(eid, state);
			return;
		}
		float dist = glm::length(target->position - entity.position);
		if (dist > 2.5f) {
			glm::vec3 dir = glm::normalize(target->position - entity.position);
			sendMove(eid, dir * entity.def().walk_speed, state.goalText);
		} else {
			sendStopMove(eid, entity, state.goalText);
			// TODO: send Convert for melee attack
			advanceStep(eid, state);
		}
	}

	void executeRelocateStep(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		// TODO: walk to container, then send Relocate
		ActionProposal p;
		p.type = ActionProposal::Relocate;
		p.actorId = eid;
		p.relocateFrom = step.relocateFrom;
		p.relocateTo = step.relocateTo;
		p.itemId = step.itemId;
		p.itemCount = step.itemCount;
		m_server.sendAction(p);
		advanceStep(eid, state);
	}

	void advanceStep(EntityId eid, AgentState& state) {
		state.stepIndex++;
		state.waypoints.clear();
		state.wpIndex = 0;
	}

	// ── Action helpers ──────────────────────────────────────────────────

	void sendMove(EntityId eid, glm::vec3 vel, const std::string& goal) {
		// Client-side prediction (same pattern as gameplay_movement.cpp for
		// the player): update the entity's velocity locally so physics runs
		// immediately. Yaw is derived from velocity each tick in
		// network_server.h (mirror of gameplay_movement.cpp:297) — setting
		// it here would only provide one snap at decision boundaries.
		if (Entity* e = m_server.getEntity(eid)) {
			e->velocity.x = vel.x;
			e->velocity.z = vel.z;
		}

		ActionProposal p;
		p.type = ActionProposal::Move;
		p.actorId = eid;
		p.desiredVel = vel;
		p.goalText = goal;
		m_server.sendAction(p);
	}

	void sendStopMove(EntityId eid, Entity& entity, const std::string& goal) {
		sendMove(eid, {0, 0, 0}, goal);
	}

	// ── Straight-line movement: just one waypoint at the destination ────
	// Pathfinding removed intentionally. Entities walk directly at their
	// goal; if they hit a wall they get stuck. Collision is the server's
	// concern, not the AI's.

	std::vector<glm::vec3> generateWaypoints(glm::vec3 /*from*/, glm::vec3 to) {
		return { to };
	}

	// ── Visualization rebuild ───────────────────────────────────────────

	void rebuildViz(EntityId eid, AgentState& state, Entity& entity) {
		state.viz.waypoints.clear();
		state.viz.hasAction = false;

		if (state.plan.empty()) return;

		// Build full path: entity position → each Move step target
		glm::vec3 cursor = entity.position;
		for (auto& step : state.plan) {
			if (step.type == PlanStep::Move) {
				auto wps = generateWaypoints(cursor, step.targetPos);
				for (auto& wp : wps)
					state.viz.waypoints.push_back(wp);
				cursor = step.targetPos;
			} else {
				// Non-move action at current cursor position
				state.viz.actionPos = step.targetPos;
				state.viz.actionType = step.type;
				state.viz.hasAction = true;
				state.viz.waypoints.push_back(step.targetPos);
				cursor = step.targetPos;
			}
		}
	}

	// ── Event-driven decide-loop API — TODO(decide-loop) ────────────────
	//
	// These replace executeMoveStep/executeHarvestStep/executeAttackStep/
	// executeRelocateStep in Step 5. Signatures committed now so reviewers
	// see the shape; bodies are pseudocode-only until their step lands.

	// Step 5 — classify a step by observable world state only.
	//   Pseudocode:
	//     switch (step.type):
	//       Move:     if arrived(target): return Success
	//                 if stillAccum>STUCK_SECONDS: return Failed("stuck")
	//                 return InProgress
	//       Harvest:  if blockAt(target)==air: return Success
	//                 if out_of_reach(target): return Failed("out_of_range")
	//                 return InProgress
	//       Attack:   if !target or target.removed: return Success
	//                 if out_of_reach(target): return Failed("fled")
	//                 return InProgress
	//       Rest/Graze/Sleep: progress += dt
	//                 if progress >= step.duration: return Success
	//                 return InProgress
	//       Relocate: if inventoryChangeMatches(...): return Success
	//                 return InProgress
	StepOutcome evaluateStep(const PlanStep& /*step*/, Entity& /*entity*/,
	                         AgentState& /*state*/, float /*dt*/) {
		// TODO(decide-loop) Step 5: implement per pseudocode above.
		return StepOutcome::InProgress;
	}

	// Step 5 — emit ActionProposal for a step currently InProgress.
	//   Pseudocode:
	//     Move:     sendMove(eid, dir*speed, goal)
	//     Harvest:  if in_reach: sendStopMove + sendConvert(block→nothing)
	//               else:         sendMove toward block
	//     Attack:   if in_reach: sendStopMove + sendConvert(target.hp→0)
	//               else:         sendMove toward target
	//     Rest/Graze/Sleep: sendStopMove (idle)
	//     Relocate: sendRelocate(...)
	void applyStep(EntityId /*eid*/, AgentState& /*state*/, Entity& /*entity*/,
	               const PlanStep& /*step*/) {
		// TODO(decide-loop) Step 5: implement per pseudocode above.
	}

	// Step 5 — install a plan that came back from the worker (or from
	// legacy main-thread decide). Only place rebuildViz() is called:
	// the plan is immutable between re-decides, so viz never flickers.
	//   Pseudocode:
	//     state.plan = result.plan
	//     state.stepIndex = 0
	//     state.waypoints.clear(); state.wpIndex = 0
	//     state.watch = StepWatch{}
	//     state.goalText = result.goalText
	//     rebuildViz(eid, state, entity)
	void installPlan(EntityId /*eid*/, AgentState& /*state*/, Entity& /*entity*/,
	                 Plan /*plan*/, const std::string& /*goalText*/) {
		// TODO(decide-loop) Step 5: implement per pseudocode above.
	}

	// Step 6 — detect client-side interrupts (HP drop, target gone, ...)
	// by diffing previous-frame and current-frame snapshots.
	//   Pseudocode:
	//     for each agent:
	//       if entity.hp < state.prevHp:
	//           m_decisionQueue.enqueue(eid, {Success, goal, step.type, "interrupt:hp"})
	//           state.plan.clear()
	//       if step.type==Attack and target missing:
	//           m_decisionQueue.enqueue(eid, {Success, goal, Attack, "interrupt:target_gone"})
	//           state.plan.clear()
	//       state.prevHp = entity.hp
	void scanClientInterrupts() {
		// TODO(decide-loop) Step 6: implement per pseudocode above.
	}

	// Step 7 — handlers for server-broadcast interrupts (proximity edge,
	// world events like day/night). Called from network_server.h when
	// S_NPC_INTERRUPT / S_WORLD_EVENT arrive.
public:
	//   Pseudocode:
	//     void onInterrupt(eid, reason):
	//         if m_agents.find(eid) != end:
	//             m_decisionQueue.enqueue(eid,
	//                 {Success, state.goalText, step.type, "interrupt:"+reason})
	//             state.plan.clear()
	void onInterrupt(EntityId /*eid*/, const std::string& /*reason*/) {
		// TODO(decide-loop) Step 7: implement per pseudocode above.
	}

	//   Pseudocode:
	//     void onWorldEvent(kind, payload):
	//         for each agent that cares about `kind` (e.g. day/night-sensitive):
	//             m_decisionQueue.enqueue(eid, {Success, goal, type, "interrupt:"+kind})
	//             state.plan.clear()
	void onWorldEvent(const std::string& /*kind*/, const std::string& /*payload*/) {
		// TODO(decide-loop) Step 7: implement per pseudocode above.
	}
private:

	// ── Helpers ─────────────────────────────────────────────────────────

	BehaviorHandle loadBehaviorForEntity(const std::string& behaviorId) {
		std::string src = m_behaviors.load(behaviorId);
		if (src.empty()) {
			printf("[AgentClient] No behavior source for '%s'\n", behaviorId.c_str());
			return -1;
		}
		std::string err;
		BehaviorHandle h = pythonBridge().loadBehavior(src, err);
		if (h < 0) {
			printf("[AgentClient] Failed to load '%s': %s\n", behaviorId.c_str(), err.c_str());
		}
		return h;
	}

	std::vector<NearbyEntity> gatherNearbyFromServer(const Entity& self) {
		std::vector<NearbyEntity> result;
		m_server.forEachEntity([&](Entity& e) {
			if (e.id() == self.id() || e.removed) return;
			glm::vec3 delta = e.position - self.position;
			float dist = glm::length(delta);
			if (dist > 64.0f) return;
			NearbyEntity ne;
			ne.id = e.id(); ne.typeId = e.typeId();
			ne.kind = e.def().kind; ne.position = e.position;
			ne.distance = dist; ne.hp = e.hp();
			ne.tags = e.def().tags;
			result.push_back(ne);
		});
		return result;
	}

	// ── Members ─────────────────────────────────────────────────────────

	ServerInterface& m_server;
	BehaviorStore& m_behaviors;
	std::unordered_map<EntityId, AgentState> m_agents;
	DecisionQueue m_decisionQueue;
	std::mt19937 m_rng;
	float m_time = 0;
	float m_discoveryTimer = 100.0f; // trigger immediate discovery on first tick
	int m_lastDecidesRun = 0;

	// ── Worker-thread decide loop ──────────────────────────────────────
	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace modcraft

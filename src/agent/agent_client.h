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
		: m_server(server), m_behaviors(behaviors), m_rng(std::random_device{}()) {}

	// ── Main tick (called from Game::updatePlaying) ──────────────────────

	void tick(float dt) {
		using Clock = std::chrono::steady_clock;
		m_time += dt;
		auto t0 = Clock::now();
		discoverEntities();
		auto t1 = Clock::now();
		phaseDecide();
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

		// Timing
		bool claimed = false;
		bool needsDecide = true;
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
				m_decisionQueue.scheduleNow(e.id());
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
							m_decisionQueue.scheduleNow(it->first);
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

		auto ready = m_decisionQueue.drain(8);
		m_lastDecidesRun = 0;
		for (EntityId eid : ready) {
			float elapsedMs = std::chrono::duration<float, std::milli>(
				Clock::now() - phaseStart).count();
			if (elapsedMs > kBudgetMs) {
				m_decisionQueue.scheduleNow(eid); // re-queue for next tick
				continue;
			}
			m_lastDecidesRun++;
			auto ait = m_agents.find(eid);
			if (ait == m_agents.end()) continue;
			if (!ait->second.claimed) continue;

			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			runDecide(eid, ait->second, *e);
		}
	}

	void runDecide(EntityId eid, AgentState& state, Entity& entity) {
		if (state.handle < 0) {
			// Try reloading
			state.handle = loadBehaviorForEntity(state.behaviorId);
			if (state.handle < 0) {
				m_decisionQueue.schedule(eid,
					SteadyClock::now() + std::chrono::seconds(5));
				return;
			}
		}

		// Gather nearby entities
		auto nearby = gatherNearbyFromServer(entity);

		// Block query callback (reads from shared chunk cache)
		auto blockQuery = [&](int x, int y, int z) -> std::string {
			auto& chunks = m_server.chunks();
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
			return m_server.blockRegistry().get(bid).string_id;
		};

		std::string goalOut, errorOut;
		Plan plan = pythonBridge().callDecide(
			state.handle, entity, nearby, 0.25f,
			m_server.worldTime(), goalOut, errorOut, blockQuery);

		if (!errorOut.empty()) {
			entity.goalText = "ERROR: " + errorOut.substr(0, 60);
			entity.hasError = true;
			entity.errorText = errorOut;
			m_decisionQueue.schedule(eid,
				SteadyClock::now() + std::chrono::seconds(2));
			return;
		}

		entity.goalText = goalOut;
		entity.hasError = false;
		entity.errorText.clear();

		// Install new plan
		state.plan = std::move(plan);
		state.stepIndex = 0;
		state.goalText = goalOut;
		state.waypoints.clear();
		state.wpIndex = 0;

		// Build visualization
		rebuildViz(eid, state, entity);

		// Schedule next decide (500ms — pathfinding is expensive on main thread)
		m_decisionQueue.schedule(eid,
			SteadyClock::now() + std::chrono::milliseconds(500));
	}

	// ── Phase 2: EXECUTE ────────────────────────────────────────────────

	void phaseExecute(float dt) {
		for (auto& [eid, state] : m_agents) {
			if (!state.claimed) continue;
			if (state.plan.empty()) continue;
			if (state.stepIndex >= (int)state.plan.size()) {
				// Plan complete → re-decide
				state.plan.clear();
				state.viz.waypoints.clear();
				if (!m_decisionQueue.hasPending(eid))
					m_decisionQueue.scheduleNow(eid);
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
		// the player): update the entity's velocity and facing direction
		// locally so the render loop sees them immediately, without waiting
		// for the server round-trip. The server remains authoritative — it
		// will correct position via broadcast — but facing/animation should
		// track input as snappy as the player's does.
		if (Entity* e = m_server.getEntity(eid)) {
			e->velocity.x = vel.x;
			e->velocity.z = vel.z;
			if (std::abs(vel.x) > 0.01f || std::abs(vel.z) > 0.01f)
				e->yaw = glm::degrees(std::atan2(vel.z, vel.x));
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
};

} // namespace modcraft

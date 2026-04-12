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
		scanClientInterrupts();
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

		// Visualization (rebuilt each time a new plan arrives)
		PlanViz viz;

		bool claimed = false;

		// Per-step observable-outcome bookkeeping. evaluateStep() uses these
		// to detect Success/Failed from world state only. `initialized` resets
		// to false on each stepIndex advance via advanceStep().
		struct StepWatch {
			bool        initialized   = false;
			bool        modeDetected  = false;  // first-tick mode latch (Move idle vs. travel)
			bool        isIdleHold    = false;  // Move target == current pos → hold for kIdleHoldSeconds
			float       stillAccum    = 0.0f;   // seconds with ~zero horizontal speed
			float       progress      = 0.0f;   // for duration-based actions
			int         prevTargetHP  = 0;      // Attack
			std::string failReason;
		};
		StepWatch watch;

		// ── Client-side interrupt diff snapshot — Step 6 ──
		int prevHp = 0;

		// Player-override pause: when the local player clicks to move a
		// claimed NPC, we install a synthetic Move plan and arm this timer.
		// On plan completion (finishPlan), decide() is NOT re-queued until
		// the timer expires — giving the NPC a visible "I'm obeying you"
		// idle beat before its Python behavior resumes.
		float overridePauseTimer  = 0.0f;
		bool  hasPendingOutcome   = false;
		LastOutcome pendingOutcome;
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
			state.watch = AgentState::StepWatch{};

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
	//
	// Each step is evaluated against observable world state every tick.
	// evaluateStep returns Success/Failed/InProgress from data — no timers
	// except action-internal durations (via StepWatch::progress).
	// applyStep sends the ActionProposal for an InProgress step.
	// Terminal outcomes (Success on final step, Failed anywhere) enqueue
	// a re-decide with LastOutcome describing what happened.

	static constexpr float kArriveEps       = 1.5f;   // blocks
	static constexpr float kStillEps        = 0.1f;   // speed blocks/s
	static constexpr float kStuckSeconds    = 2.0f;
	static constexpr float kReachHarvest    = 3.0f;
	static constexpr float kReachAttack     = 2.5f;
	// When a behavior returns Move(entity.x, y, z) (stand-still), the plan would
	// Success in one frame and thrash the decide loop at ~60 Hz. Hold idle Moves
	// for this long before re-deciding — lets the behavior check "am I still
	// near my target?" at a human-reasonable cadence, not per frame.
	static constexpr float kIdleHoldSeconds = 1.0f;

	void phaseExecute(float dt) {
		for (auto& [eid, state] : m_agents) {
			if (!state.claimed) continue;

			// Drain player-override pause; once it expires, flush the
			// deferred outcome so Python decide() can pick up control.
			if (state.overridePauseTimer > 0.0f) {
				state.overridePauseTimer -= dt;
				if (state.overridePauseTimer <= 0.0f) {
					state.overridePauseTimer = 0.0f;
					if (state.hasPendingOutcome) {
						state.hasPendingOutcome = false;
						if (!m_decisionQueue.hasPending(eid))
							m_decisionQueue.enqueue(eid, std::move(state.pendingOutcome));
					}
				}
			}

			if (state.plan.empty()) continue;

			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			if (state.stepIndex >= (int)state.plan.size()) {
				finishPlan(eid, state, StepOutcome::Success, {});
				continue;
			}

			PlanStep& step = state.plan[state.stepIndex];
			if (!state.watch.initialized) {
				state.watch = AgentState::StepWatch{};
				state.watch.initialized = true;
				if (step.type == PlanStep::Attack) {
					if (Entity* t = m_server.getEntity(step.targetEntity))
						state.watch.prevTargetHP = t->hp();
				}
			}

			StepOutcome outcome = evaluateStep(step, *e, state.watch, dt);

			switch (outcome) {
			case StepOutcome::InProgress:
				applyStep(eid, state, *e, step);
				break;
			case StepOutcome::Success:
				advanceStep(state);
				if (state.stepIndex >= (int)state.plan.size())
					finishPlan(eid, state, StepOutcome::Success, {});
				break;
			case StepOutcome::Failed:
				finishPlan(eid, state, StepOutcome::Failed,
				           state.watch.failReason);
				break;
			}
		}
	}

	StepOutcome evaluateStep(PlanStep& step, Entity& entity,
	                         AgentState::StepWatch& watch, float dt) {
		switch (step.type) {
		case PlanStep::Move:     return evaluateMove(step, entity, watch, dt);
		case PlanStep::Harvest:  return evaluateHarvest(step, entity, watch, dt);
		case PlanStep::Attack:   return evaluateAttack(step, entity, watch, dt);
		case PlanStep::Relocate: return evaluateRelocate(step, entity, watch, dt);
		}
		return StepOutcome::Success;
	}

	StepOutcome evaluateMove(PlanStep& step, Entity& entity,
	                         AgentState::StepWatch& watch, float dt) {
		glm::vec3 delta = step.targetPos - entity.position;
		delta.y = 0;
		float dist = glm::length(delta);

		// Latch mode on the first evaluation: if the behavior returned a
		// stand-still Move (target == current pos), enter idle-hold. Applied
		// Move will zero velocity; plan Successes after kIdleHoldSeconds.
		if (!watch.modeDetected) {
			watch.modeDetected = true;
			watch.isIdleHold   = (dist < kArriveEps);
		}

		if (watch.isIdleHold) {
			watch.progress += dt;
			if (watch.progress >= kIdleHoldSeconds) return StepOutcome::Success;
			return StepOutcome::InProgress;
		}

		if (dist < kArriveEps) return StepOutcome::Success;

		float horiz = std::sqrt(entity.velocity.x * entity.velocity.x +
		                        entity.velocity.z * entity.velocity.z);
		if (horiz < kStillEps) {
			watch.stillAccum += dt;
			if (watch.stillAccum > kStuckSeconds) {
				watch.failReason = "stuck";
				return StepOutcome::Failed;
			}
		} else {
			watch.stillAccum = 0;
		}
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateHarvest(PlanStep& step, Entity& entity,
	                            AgentState::StepWatch& watch, float /*dt*/) {
		glm::ivec3 bp = glm::ivec3(glm::floor(step.targetPos));
		BlockId bid = m_server.chunks().getBlock(bp.x, bp.y, bp.z);
		const BlockDef& bd = m_server.blockRegistry().get(bid);
		if (bd.string_id == "base:air") return StepOutcome::Success;

		glm::vec3 delta = step.targetPos - entity.position;
		delta.y = 0;
		if (glm::length(delta) > kReachHarvest + 2.0f) {
			// Walk closer — applyStep handles the move; stuck-detect applies.
			float horiz = std::sqrt(entity.velocity.x * entity.velocity.x +
			                        entity.velocity.z * entity.velocity.z);
			if (horiz < kStillEps) {
				watch.stillAccum += 1.0f / 60.0f; // dt-free fallback
				if (watch.stillAccum > kStuckSeconds) {
					watch.failReason = "stuck";
					return StepOutcome::Failed;
				}
			} else watch.stillAccum = 0;
		}
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateAttack(PlanStep& step, Entity& /*entity*/,
	                           AgentState::StepWatch& watch, float /*dt*/) {
		Entity* target = m_server.getEntity(step.targetEntity);
		if (!target || target->removed) {
			// Target gone — treat as Success (kill or disappear)
			return StepOutcome::Success;
		}
		if (target->hp() <= 0) return StepOutcome::Success;
		watch.prevTargetHP = target->hp();
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateRelocate(PlanStep& /*step*/, Entity& /*entity*/,
	                             AgentState::StepWatch& /*watch*/, float /*dt*/) {
		// Relocate is one-shot: applyStep sends the proposal, step advances
		// unconditionally on the next tick via Success.
		return StepOutcome::Success;
	}

	void applyStep(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		switch (step.type) {
		case PlanStep::Move:
			applyMove(eid, state, entity, step);
			break;
		case PlanStep::Harvest:
			applyHarvest(eid, state, entity, step);
			break;
		case PlanStep::Attack:
			applyAttack(eid, state, entity, step);
			break;
		case PlanStep::Relocate:
			applyRelocate(eid, state, step);
			break;
		}
	}

	void applyMove(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		glm::vec3 dir = step.targetPos - entity.position;
		dir.y = 0;
		float dist = glm::length(dir);
		// Idle-hold or already-at-target: zero velocity so residual drift from
		// a prior travel plan doesn't carry the entity past its target.
		if (dist < kArriveEps) {
			sendStopMove(eid, entity, state.goalText);
			return;
		}
		dir /= dist;
		float speed = step.speed > 0 ? step.speed : entity.def().walk_speed;
		sendMove(eid, dir * speed, state.goalText);
	}

	void applyHarvest(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		glm::vec3 delta = step.targetPos - entity.position;
		delta.y = 0;
		float dist = glm::length(delta);
		if (dist > kReachHarvest) {
			glm::vec3 dir = glm::normalize(delta);
			sendMove(eid, dir * entity.def().walk_speed, state.goalText);
			return;
		}
		sendStopMove(eid, entity, state.goalText);
		ActionProposal p;
		p.type = ActionProposal::Convert;
		p.actorId = eid;
		p.fromItem = ""; p.fromCount = 0;
		p.toItem = ""; p.toCount = 0;
		p.convertFrom = Container::block(glm::ivec3(step.targetPos));
		p.convertInto = Container::self();
		m_server.sendAction(p);
	}

	void applyAttack(EntityId eid, AgentState& state, Entity& entity, PlanStep& step) {
		Entity* target = m_server.getEntity(step.targetEntity);
		if (!target || target->removed) return;
		glm::vec3 delta = target->position - entity.position;
		delta.y = 0;
		float dist = glm::length(delta);
		if (dist > kReachAttack) {
			glm::vec3 dir = glm::normalize(delta);
			sendMove(eid, dir * entity.def().walk_speed, state.goalText);
		} else {
			sendStopMove(eid, entity, state.goalText);
			// TODO: melee damage Convert — currently no attack proposal.
		}
	}

	void applyRelocate(EntityId eid, AgentState& /*state*/, PlanStep& step) {
		ActionProposal p;
		p.type = ActionProposal::Relocate;
		p.actorId = eid;
		p.relocateFrom = step.relocateFrom;
		p.relocateTo = step.relocateTo;
		p.itemId = step.itemId;
		p.itemCount = step.itemCount;
		m_server.sendAction(p);
	}

	void advanceStep(AgentState& state) {
		state.stepIndex++;
		state.watch = AgentState::StepWatch{};
	}

	// Terminate the current plan and enqueue a re-decide with outcome info.
	void finishPlan(EntityId eid, AgentState& state, StepOutcome outcome,
	                const std::string& reason) {
		PlanStep::Type lastType = PlanStep::Move;
		if (!state.plan.empty())
			lastType = state.plan[std::min(state.stepIndex,
			                               (int)state.plan.size() - 1)].type;
		// On Move-Success, zero velocity so residual drift doesn't carry the
		// entity past its target while the worker computes the next plan
		// (decide runs async — can be multiple frames in flight).
		if (lastType == PlanStep::Move && outcome == StepOutcome::Success) {
			if (Entity* e = m_server.getEntity(eid))
				sendStopMove(eid, *e, state.goalText);
		}
		LastOutcome next;
		next.outcome     = outcome;
		next.goalText    = state.goalText;
		next.stepTypeRaw = (int)lastType;
		next.reason      = reason;
		state.plan.clear();
		state.stepIndex  = 0;
		state.viz.waypoints.clear();
		// If player override is active, hold off on re-decide until the
		// pause timer drains (see phaseExecute). The pending outcome is
		// stashed so the eventual enqueue carries "interrupt:player_override".
		if (state.overridePauseTimer > 0.0f) {
			next.reason = "interrupt:player_override";
			state.pendingOutcome = std::move(next);
			state.hasPendingOutcome = true;
			return;
		}
		if (!m_decisionQueue.hasPending(eid))
			m_decisionQueue.enqueue(eid, std::move(next));
	}

	// ── Client-side interrupt diff ──────────────────────────────────────
	// Observe world state each tick; if something reasonably urgent
	// happened (HP drop, attack target vanished), interrupt the current
	// plan and enqueue a re-decide. Visual-only races are fine.
	void scanClientInterrupts() {
		for (auto& [eid, state] : m_agents) {
			if (!state.claimed) continue;
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			int curHp = e->hp();
			bool hpDropped = (state.prevHp > 0 && curHp < state.prevHp);
			state.prevHp = curHp;

			if (state.plan.empty()) continue;

			if (hpDropped) {
				interruptPlan(eid, state, *e, "hp");
				continue;
			}

			if (state.stepIndex < (int)state.plan.size()) {
				PlanStep& step = state.plan[state.stepIndex];
				if (step.type == PlanStep::Attack) {
					Entity* t = m_server.getEntity(step.targetEntity);
					if (!t || t->removed) {
						interruptPlan(eid, state, *e, "target_gone");
						continue;
					}
				}
			}
		}
	}

public:
	// Called in-process when the local player click-drives a claimed NPC
	// (RTS/RPG right-click). Replaces any in-flight plan with a single
	// Move to the clicked goal, arms a 3.14 s idle pause, and refreshes
	// viz so the dash line snaps to the new target this frame. The
	// re-decide is deferred until the pause expires (see phaseExecute
	// + finishPlan).
	void onOverride(EntityId eid, glm::vec3 goal) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		AgentState& state = it->second;

		state.plan.clear();
		state.stepIndex = 0;
		state.watch = AgentState::StepWatch{};
		state.plan.push_back(PlanStep::move(goal));
		state.goalText = "player_override";
		state.overridePauseTimer = 3.14f;
		rebuildViz(eid, state, *e);
	}

	// Called from network layer when server broadcasts a per-entity
	// interrupt (e.g. proximity trigger). See Step 7.
	void onInterrupt(EntityId eid, const std::string& reason) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		interruptPlan(eid, it->second, *e, reason);
	}

	// Called from network layer when server broadcasts a world-wide
	// event (e.g. day/night edge). See Step 7.
	void onWorldEvent(const std::string& kind, const std::string& /*payload*/) {
		std::string reason = kind;
		for (auto& [eid, state] : m_agents) {
			if (!state.claimed) continue;
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			interruptPlan(eid, state, *e, reason);
		}
	}
private:

	// Abort the current plan and enqueue re-decide with interrupt:<reason>.
	void interruptPlan(EntityId eid, AgentState& state, Entity& /*entity*/,
	                   const std::string& reason) {
		if (state.plan.empty()) return;
		PlanStep::Type lastType = state.plan[std::min(state.stepIndex,
		                                              (int)state.plan.size() - 1)].type;
		LastOutcome next;
		next.outcome     = StepOutcome::Success;
		next.goalText    = state.goalText;
		next.stepTypeRaw = (int)lastType;
		next.reason      = "interrupt:" + reason;
		state.plan.clear();
		state.stepIndex  = 0;
		state.viz.waypoints.clear();
		m_decisionQueue.enqueue(eid, std::move(next));
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

	// ── Visualization rebuild ───────────────────────────────────────────
	// Straight-line: entities walk directly at their goal. Collision is
	// the server's concern, not the AI's.

	void rebuildViz(EntityId /*eid*/, AgentState& state, Entity& /*entity*/) {
		state.viz.waypoints.clear();
		state.viz.hasAction = false;

		if (state.plan.empty()) return;

		for (auto& step : state.plan) {
			if (step.type == PlanStep::Move) {
				state.viz.waypoints.push_back(step.targetPos);
			} else {
				state.viz.actionPos = step.targetPos;
				state.viz.actionType = step.type;
				state.viz.hasAction = true;
				state.viz.waypoints.push_back(step.targetPos);
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

	// ── Worker-thread decide loop ──────────────────────────────────────
	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace modcraft

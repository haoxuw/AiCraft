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
#include "shared/move_stuck_log.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <random>

namespace civcraft {

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
		phaseAutoPickup();
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
		// controlled NPC, we install a synthetic Move plan and arm this timer.
		// On plan completion (finishPlan), decide() is NOT re-queued until
		// the timer expires — giving the NPC a visible "I'm obeying you"
		// idle beat before its Python behavior resumes.
		float overridePauseTimer  = 0.0f;
		bool  hasPendingOutcome   = false;
		LastOutcome pendingOutcome;

		// Tier-1 periodic rethink backstop. Counts time since the current
		// plan was installed; when it crosses kPeriodicRethinkSec, the
		// agent is interrupted with reason="periodic" so Python's rule
		// list re-evaluates against the current world state. Catches any
		// condition that flipped mid-plan (e.g. Threatened becoming true
		// while Rejoin is walking) without special-casing per condition.
		// Reset to 0 whenever state.plan is replaced.
		float timeSinceDecide = 0.0f;

		// Stuck detection: rolling window of (last position sample, seconds since
		// last notable progress). If velocity stays non-zero but position doesn't
		// advance, we emit a [MoveStuck:Agent-Stuck] diagnostic — this is the
		// canonical "wants to move, not moving" signal the user cares about.
		glm::vec3 lastSampledPos   = glm::vec3(0.0f);
		float     stuckAccum       = 0.0f;
		bool      stuckLogged      = false;
	};

	// ── Entity discovery ────────────────────────────────────────────────

	void discoverEntities() {
		// Periodic scan: track NPCs with BehaviorId owned by this player.
		// Ownership is baked in at spawn (server::spawnMobsForClient), so we
		// never send a claim — we just register the entities the server
		// already assigned to us.
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
			if (m_agents.count(e.id())) return;
			if (e.getProp<int>(Prop::Owner, 0) != (int)myId) return;

			AgentState state;
			state.behaviorId = bid;
			state.handle = loadBehaviorForEntity(bid);
			m_agents[e.id()] = std::move(state);
			if (m_agents[e.id()].handle >= 0)
				m_decisionQueue.enqueue(e.id());
		});

		// Drop agents whose entity disappeared.
		for (auto it = m_agents.begin(); it != m_agents.end(); ) {
			Entity* e = m_server.getEntity(it->first);
			if (!e || e->removed) {
				if (it->second.handle >= 0)
					pythonBridge().unloadBehavior(it->second.handle);
				m_decisionQueue.remove(it->first);
				it = m_agents.erase(it);
			} else {
				++it;
			}
		}
	}

	// ── Phase: AUTO-PICKUP ───────────────────────────────────────────────
	//
	// Every tick, for each controlled agent, scan nearby ItemEntities and
	// send a Relocate proposal to pick them up when the agent has capacity.
	// The *server* runs the same canAccept() check (shared/inventory.h) to
	// validate, so this is purely a client-side intent — no authority.
	// Behaviors don't need to emit pickup actions themselves; the engine
	// handles it uniformly across all Living types.

	void phaseAutoPickup() {
		// Throttle: at most once per 0.25s per agent — pickup rate is bounded
		// by server validation anyway, but this avoids spamming proposals.
		m_autoPickupTimer += 1.0f / 60.0f;
		if (m_autoPickupTimer < 0.25f) return;
		m_autoPickupTimer = 0.0f;

		for (auto& [eid, state] : m_agents) {
			Entity* actor = m_server.getEntity(eid);
			if (!actor || actor->removed || !actor->inventory) continue;
			// Only humanoids auto-pickup. Animals ignore ground items so e.g.
			// chickens don't instantly re-grab their own eggs.
			if (!actor->def().hasTag("humanoid")) continue;
			float cap = actor->def().inventory_capacity;
			if (cap <= 0.0f) continue;
			float range = actor->def().pickup_range > 0.0f
			              ? actor->def().pickup_range : 1.5f;

			EntityId bestItemId = 0;
			float    bestDist2  = range * range;
			m_server.forEachEntity([&](Entity& e) {
				if (e.removed) return;
				if (e.typeId() != ItemName::ItemEntity) return;
				glm::vec3 d = e.position - actor->position;
				float d2 = glm::dot(d, d);
				if (d2 > bestDist2) return;
				std::string itemType = e.getProp<std::string>(Prop::ItemType);
				int count = e.getProp<int>(Prop::Count, 1);
				if (!actor->inventory->canAccept(itemType, count, cap)) return;
				bestDist2  = d2;
				bestItemId = e.id();
			});
			if (!bestItemId) continue;

			ActionProposal p;
			p.type = ActionProposal::Relocate;
			p.actorId = eid;
			p.relocateFrom = Container::entity(bestItemId);
			p.relocateTo   = Container::self();
			m_server.sendAction(p);
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
			ChunkPos cp = worldToChunk(x, y, z);
			auto* chunk = chunks.getChunkIfLoaded(cp);
			if (!chunk) return "air";
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			BlockId bid = chunk->get(lx, ly, lz);
			return srv->blockRegistry().get(bid).string_id;
		};

		// Scan loaded chunks around `origin` for blocks matching `typeId`,
		// returning up to `maxResults` nearest matches (Chebyshev-bounded
		// by maxDist on XZ). Runs on the worker thread; stale reads are
		// acceptable per project policy (visual-only race).
		req.scanBlocks = [srv](const std::string& typeId, glm::vec3 origin,
		                       float maxDist, int maxResults)
		    -> std::vector<BlockSample> {
			std::vector<BlockSample> out;
			auto& reg = srv->blockRegistry();
			BlockId want = reg.getId(typeId);
			if (want == BLOCK_AIR) return out;
			auto& chunks = srv->chunks();
			int cxMin = (int)std::floor((origin.x - maxDist) / (float)CHUNK_SIZE);
			int cxMax = (int)std::floor((origin.x + maxDist) / (float)CHUNK_SIZE);
			int czMin = (int)std::floor((origin.z - maxDist) / (float)CHUNK_SIZE);
			int czMax = (int)std::floor((origin.z + maxDist) / (float)CHUNK_SIZE);
			int cyMin = (int)std::floor((origin.y - maxDist) / (float)CHUNK_SIZE);
			int cyMax = (int)std::floor((origin.y + maxDist) / (float)CHUNK_SIZE);
			float maxDist2 = maxDist * maxDist;
			for (int cx = cxMin; cx <= cxMax; cx++)
			for (int cz = czMin; cz <= czMax; cz++)
			for (int cy = cyMin; cy <= cyMax; cy++) {
				Chunk* chunk = chunks.getChunkIfLoaded({cx, cy, cz});
				if (!chunk) continue;
				for (int ly = 0; ly < CHUNK_SIZE; ly++)
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
				for (int lx = 0; lx < CHUNK_SIZE; lx++) {
					if (chunk->get(lx, ly, lz) != want) continue;
					int wx = cx * CHUNK_SIZE + lx;
					int wy = cy * CHUNK_SIZE + ly;
					int wz = cz * CHUNK_SIZE + lz;
					float dx = (wx + 0.5f) - origin.x;
					float dz = (wz + 0.5f) - origin.z;
					float d2 = dx*dx + dz*dz;
					if (d2 > maxDist2) continue;
					BlockSample s;
					s.typeId = typeId;
					s.x = wx; s.y = wy; s.z = wz;
					s.distance = std::sqrt(d2);
					out.push_back(s);
				}
			}
			std::sort(out.begin(), out.end(),
			          [](const BlockSample& a, const BlockSample& b) {
			              return a.distance < b.distance;
			          });
			if ((int)out.size() > maxResults) out.resize(maxResults);
			return out;
		};

		// Scan all server-side entities by typeId within maxDist of origin.
		// Used by Python behaviors that need world-wide entity lookup (e.g.
		// villager finding any chest to deposit into — bypasses the 64-block
		// per-agent nearby cache).
		req.scanEntities = [srv](const std::string& typeId, glm::vec3 origin,
		                         float maxDist, int maxResults)
		    -> std::vector<NearbyEntity> {
			std::vector<NearbyEntity> out;
			float maxDist2 = maxDist * maxDist;
			srv->forEachEntity([&](Entity& e) {
				if (e.removed) return;
				if (e.typeId() != typeId) return;
				glm::vec3 d = e.position - origin;
				float d2 = glm::dot(d, d);
				if (d2 > maxDist2) return;
				NearbyEntity ne;
				ne.id = e.id(); ne.typeId = e.typeId();
				ne.kind = e.def().kind; ne.position = e.position;
				ne.distance = std::sqrt(d2); ne.hp = e.hp();
				ne.tags = e.def().tags;
				out.push_back(ne);
			});
			std::sort(out.begin(), out.end(),
			          [](const NearbyEntity& a, const NearbyEntity& b) {
			              return a.distance < b.distance;
			          });
			if ((int)out.size() > maxResults) out.resize(maxResults);
			return out;
		};

		// Scan block decorations (flowers, moss, …) by typeId. Implemented
		// via ServerInterface::annotationsForChunk; iterates loaded chunks in
		// a Chebyshev box around origin and filters by typeId + maxDist.
		req.scanAnnotations = [srv](const std::string& typeId, glm::vec3 origin,
		                            float maxDist, int maxResults)
		    -> std::vector<BlockSample> {
			std::vector<BlockSample> out;
			float maxDist2 = maxDist * maxDist;
			int cxMin = (int)std::floor((origin.x - maxDist) / (float)CHUNK_SIZE);
			int cxMax = (int)std::floor((origin.x + maxDist) / (float)CHUNK_SIZE);
			int czMin = (int)std::floor((origin.z - maxDist) / (float)CHUNK_SIZE);
			int czMax = (int)std::floor((origin.z + maxDist) / (float)CHUNK_SIZE);
			int cyMin = (int)std::floor((origin.y - maxDist) / (float)CHUNK_SIZE);
			int cyMax = (int)std::floor((origin.y + maxDist) / (float)CHUNK_SIZE);
			for (int cx = cxMin; cx <= cxMax; cx++)
			for (int cz = czMin; cz <= czMax; cz++)
			for (int cy = cyMin; cy <= cyMax; cy++) {
				ChunkPos cp{cx, cy, cz};
				const auto* list = srv->annotationsForChunk(cp);
				if (!list) continue;
				for (auto& [wpos, ann] : *list) {
					if (ann.typeId != typeId) continue;
					float dx = (wpos.x + 0.5f) - origin.x;
					float dy = (wpos.y + 0.5f) - origin.y;
					float dz = (wpos.z + 0.5f) - origin.z;
					float d2 = dx*dx + dy*dy + dz*dz;
					if (d2 > maxDist2) continue;
					BlockSample s;
					s.typeId = typeId;
					s.x = wpos.x; s.y = wpos.y; s.z = wpos.z;
					s.distance = std::sqrt(d2);
					out.push_back(s);
				}
			}
			std::sort(out.begin(), out.end(),
			          [](const BlockSample& a, const BlockSample& b) {
			              return a.distance < b.distance;
			          });
			if ((int)out.size() > maxResults) out.resize(maxResults);
			return out;
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
			state.timeSinceDecide = 0.0f;

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
		s.inventoryCapacity = e.def().inventory_capacity;
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
	// Tier-1 periodic-rethink backstop. Generic across all entities and
	// behaviors: every kPeriodicRethinkSec the current plan is interrupted
	// (reason="periodic") so Python's rule list re-evaluates. Gives
	// condition changes that no explicit event models (new predator
	// approaches, HP climb/drop that didn't cross the delta, world
	// weather, etc.) a bounded time before the agent notices.
	static constexpr float kPeriodicRethinkSec = 1.0f;

	void phaseExecute(float dt) {
		for (auto& [eid, state] : m_agents) {
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

			// Tier-1 periodic rethink: interrupt and re-decide every
			// kPeriodicRethinkSec while a plan is running. Cheap backstop
			// for any condition that flipped mid-plan and isn't covered
			// by an explicit server event or scanClientInterrupts probe.
			// Skipped while overridePauseTimer is armed — the pause is
			// the explicit "sit still" promise to the player.
			if (state.overridePauseTimer <= 0.0f) {
				state.timeSinceDecide += dt;
				if (state.timeSinceDecide >= kPeriodicRethinkSec &&
				    !m_decisionQueue.hasPending(eid)) {
					interruptPlan(eid, state, *e, "periodic");
					continue;
				}
			}

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
		if (bd.string_id == "air") return StepOutcome::Success;

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
	                             AgentState::StepWatch& watch, float /*dt*/) {
		// First evaluation: return InProgress so applyStep fires once and sends
		// the Relocate ActionProposal. modeDetected latches "already applied";
		// the next evaluation returns Success to advance past this step.
		if (!watch.modeDetected) {
			watch.modeDetected = true;
			return StepOutcome::InProgress;
		}
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
		// fromItem mirrors toItem: the value-conservation check on the
		// server needs the input side to have at least the output's value.
		// Harvesting a block of type X to produce item X is value-preserving.
		p.fromItem  = step.itemId;
		p.fromCount = step.itemCount > 0 ? step.itemCount : 1;
		p.toItem    = step.itemId;
		p.toCount   = step.itemCount > 0 ? step.itemCount : 1;
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
	// Called in-process when the local player click-drives a controlled NPC
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
		state.timeSinceDecide = 0.0f;
		rebuildViz(eid, state, *e);
	}

	// Called when the player enters Control mode on `eid`. Clears the
	// current plan so the entity stops acting under AI, and parks the
	// override-pause timer at ~forever — phaseExecute will skip plan
	// progression + re-decide while paused. The pending outcome is left
	// unset so there's nothing queued to flush on resume.
	void pauseAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		AgentState& state = it->second;
		state.plan.clear();
		state.stepIndex = 0;
		state.watch = AgentState::StepWatch{};
		state.viz.waypoints.clear();
		state.goalText = "controlled";
		state.overridePauseTimer = 1e9f;
		state.hasPendingOutcome = false;
		e->goalText = "controlled";
		rebuildViz(eid, state, *e);
	}

	// Called when the player releases Control of `eid`. Clears the pause
	// timer and enqueues an immediate decide() so Python autonomy resumes.
	void resumeAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		AgentState& state = it->second;
		state.overridePauseTimer = 0.0f;
		state.hasPendingOutcome = false;
		if (!m_decisionQueue.hasPending(eid))
			m_decisionQueue.enqueue(eid);
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
		Entity* e = m_server.getEntity(eid);
		if (e) {
			e->velocity.x = vel.x;
			e->velocity.z = vel.z;
		}

		// ── Stuck detection: velocity non-zero but position not advancing ──
		// Fires once per entity when the agent has been commanding motion
		// for >=1.5 s without meaningful displacement. Resets when the
		// entity starts moving again or the agent stops commanding motion.
		if (e) {
			auto it = m_agents.find(eid);
			if (it != m_agents.end()) {
				auto& s = it->second;
				float intent = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				float moved  = glm::length(glm::vec2(e->position.x, e->position.z) -
				                           glm::vec2(s.lastSampledPos.x, s.lastSampledPos.z));
				constexpr float kIntentThresh = 0.2f;   // agent wants motion
				constexpr float kMoveThresh   = 0.05f;  // actually displaced
				constexpr float kStuckWindow  = 1.5f;   // seconds before we yell
				const float dt = 1.0f / 60.0f;          // rough; sendMove is tick-cadenced

				if (intent > kIntentThresh && moved < kMoveThresh) {
					s.stuckAccum += dt;
					if (s.stuckAccum >= kStuckWindow && !s.stuckLogged) {
						char detail[192];
						snprintf(detail, sizeof(detail),
							"pos=(%.2f,%.2f,%.2f) intent=(%.2f,%.2f) goal=\"%s\" held=%.1fs",
							e->position.x, e->position.y, e->position.z,
							vel.x, vel.z, goal.c_str(), s.stuckAccum);
						logMoveStuck(eid, "Agent-Stuck",
							"agent held non-zero velocity but entity failed to displace "
							"(likely server collision clamp or client/server pos delta)",
							detail);
						s.stuckLogged = true;
					}
				} else {
					if (s.stuckLogged) {
						char detail[96];
						snprintf(detail, sizeof(detail),
							"pos=(%.2f,%.2f,%.2f)",
							e->position.x, e->position.y, e->position.z);
						logMoveStuck(eid, "Agent-Unstuck",
							"entity resumed displacement after prior Agent-Stuck",
							detail);
					}
					s.stuckAccum  = 0.0f;
					s.stuckLogged = false;
				}
				s.lastSampledPos = e->position;
			}
		}

		// ── DEBUG: agent Move-send probe (10-min cooldown) ──
		// First Move command the agent sends for this entity. Pair with
		// [MoveStuck:Server] to see whether the server accepts it
		// unchanged or clamps it.
		// (noisy per-entity startup diagnostic removed; Agent-Stuck path
		// above still fires when an agent genuinely fails to progress.)
		(void)e;

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
	float m_autoPickupTimer = 0.0f;
	float m_discoveryTimer = 100.0f; // trigger immediate discovery on first tick
	int m_lastDecidesRun = 0;

	// ── Worker-thread decide loop ──────────────────────────────────────
	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace civcraft

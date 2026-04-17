#pragma once

// AI controller for NPC entities. Two-phase tick: DECIDE (Python) → EXECUTE (ActionProposals).

#include "net/server_interface.h"
#include "server/behavior_store.h"
#include "server/behavior.h"
#include "server/python_bridge.h"
#include "agent/decision_queue.h"
#include "agent/behavior_executor.h"
#include "agent/decide_worker.h"
#include "debug/move_stuck_log.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <random>

namespace civcraft {

class AgentClient {
public:
	AgentClient(ServerInterface& server, BehaviorStore& behaviors)
		: m_server(server), m_behaviors(behaviors), m_rng(std::random_device{}()) {
		std::printf("[Agent] AgentClient constructed — behavior store initialized=%d\n",
			(int)m_behaviors.isInitialized());
		std::fflush(stdout);
		m_decideWorker.start();
	}

	~AgentClient() {
		std::printf("[Agent] AgentClient destructed — had %zu agents\n", m_agents.size());
		std::fflush(stdout);
		m_decideWorker.stop();
	}

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

	struct PlanViz {
		std::vector<glm::vec3> waypoints;
		PlanStep::Type actionType = PlanStep::Move;
		glm::vec3 actionPos = {0,0,0};
		bool hasAction = false;
	};

	const PlanViz* getPlanViz(EntityId id) const {
		auto it = m_agents.find(id);
		if (it == m_agents.end()) return nullptr;
		if (it->second.plan.empty()) return nullptr;
		return &it->second.viz;
	}

	// Plan progress for the inspector: step m_step of m_totalSteps, plus
	// decide/stuck timers. Everything else (goalText, behaviorId, error) is
	// already on Entity and should be read from there.
	struct PlanProgress {
		bool  registered         = false;
		int   stepIndex          = 0;
		int   totalSteps         = 0;
		float timeSinceDecide    = 0.0f;
		float stuckAccum         = 0.0f;
		float overridePauseTimer = 0.0f;
	};

	PlanProgress getPlanProgress(EntityId id) const {
		PlanProgress p;
		auto it = m_agents.find(id);
		if (it == m_agents.end()) return p;
		const AgentState& s      = it->second;
		p.registered             = true;
		p.stepIndex              = s.stepIndex;
		p.totalSteps             = (int)s.plan.size();
		p.timeSinceDecide        = s.timeSinceDecide;
		p.stuckAccum             = s.stuckAccum;
		p.overridePauseTimer     = s.overridePauseTimer;
		return p;
	}

	template<typename Fn>
	void forEachAgent(Fn&& fn) const {
		for (auto& [eid, state] : m_agents) fn(eid, state.viz);
	}

private:
	struct AgentState {
		std::string behaviorId;
		BehaviorHandle handle = -1;

		Plan plan;
		int stepIndex = 0;
		std::string goalText;

		PlanViz viz;

		// Observable-outcome bookkeeping; resets on stepIndex advance.
		struct StepWatch {
			bool        initialized   = false;
			bool        modeDetected  = false;
			bool        isIdleHold    = false;  // Move target == current pos
			float       stillAccum    = 0.0f;
			float       progress      = 0.0f;
			int         prevTargetHP  = 0;
			std::string failReason;
		};
		StepWatch watch;

		int prevHp = 0;

		// Player-override pause: Python decide() blocked until expires (visible obey beat).
		float overridePauseTimer  = 0.0f;
		bool  hasPendingOutcome   = false;
		LastOutcome pendingOutcome;

		// Periodic-rethink backstop so mid-plan condition changes get noticed.
		float timeSinceDecide = 0.0f;

		// "Wants to move, not moving" → [MoveStuck:Agent-Stuck].
		glm::vec3 lastSampledPos   = glm::vec3(0.0f);
		float     stuckAccum       = 0.0f;
		bool      stuckLogged      = false;
	};

	void discoverEntities() {
		// Ownership baked in at spawn — pure registration, no claim RPC.
		m_discoveryTimer += 0.05f;
		if (m_discoveryTimer < 2.0f && !m_agents.empty()) return;
		m_discoveryTimer = 0;

		EntityId myId = m_server.localPlayerId();
		if (myId == ENTITY_NONE) {
			agentDiagnostic(true, "no localPlayerId yet");
			return;
		}

		// Feeds "why zero agents?" diagnostic below.
		int total = 0, skipSelf = 0, skipNonLiving = 0, skipRemoved = 0;
		int skipNoBid = 0, skipAlreadyAgent = 0, skipNotOwnedByUs = 0, registered = 0;
		int ownedBySomeoneElse = 0, ownedByNobody = 0;

		m_server.forEachEntity([&](Entity& e) {
			total++;
			if (e.id() == myId) { skipSelf++; return; }
			if (!e.def().isLiving()) { skipNonLiving++; return; }
			if (e.removed) { skipRemoved++; return; }
			std::string bid = e.getProp<std::string>(Prop::BehaviorId, "");
			if (bid.empty()) { skipNoBid++; return; }
			if (m_agents.count(e.id())) { skipAlreadyAgent++; return; }
			int owner = e.getProp<int>(Prop::Owner, 0);
			if (owner != (int)myId) {
				skipNotOwnedByUs++;
				if (owner == 0) ownedByNobody++;
				else ownedBySomeoneElse++;
				return;
			}

			AgentState state;
			state.behaviorId = bid;
			state.handle = loadBehaviorForEntity(bid);
			m_agents[e.id()] = std::move(state);
			if (m_agents[e.id()].handle >= 0) {
				m_decisionQueue.enqueue(e.id());
				registered++;
			}
		});

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

		// Periodic snapshot: why/how many entities became agents.
		char msg[256];
		std::snprintf(msg, sizeof(msg),
			"myId=%u seen=%d agents=%zu (+%d new) skip[self=%d nonLiving=%d removed=%d "
			"noBid=%d dup=%d notMine=%d(nobody=%d,other=%d)]",
			(unsigned)myId, total, m_agents.size(), registered,
			skipSelf, skipNonLiving, skipRemoved,
			skipNoBid, skipAlreadyAgent, skipNotOwnedByUs,
			ownedByNobody, ownedBySomeoneElse);
		agentDiagnostic(m_agents.empty(), msg);
	}

	// Log once per kHealthySec normally, per kStuckSec when zeroAgents — surfaces
	// "why no NPCs are moving" first when diagnosing busted singleplayer.
	void agentDiagnostic(bool zeroAgents, const std::string& what) {
		constexpr float kHealthySec = 10.0f;
		constexpr float kStuckSec   = 1.0f;
		m_diagLogAccum += 2.0f;  // discoverEntities runs every ~2s
		float threshold = zeroAgents ? kStuckSec : kHealthySec;
		if (m_diagLogAccum < threshold) return;
		m_diagLogAccum = 0;
		std::printf("[Agent] discover: %s\n", what.c_str());
		std::fflush(stdout);
	}

	// Engine-level intent; server enforces canAccept().
	void phaseAutoPickup() {
		m_autoPickupTimer += 1.0f / 60.0f;
		if (m_autoPickupTimer < 0.25f) return;
		m_autoPickupTimer = 0.0f;

		for (auto& [eid, state] : m_agents) {
			Entity* actor = m_server.getEntity(eid);
			if (!actor || actor->removed || !actor->inventory) continue;
			// Humanoids only — chickens would instantly re-grab their own eggs.
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

	void phaseDecide() {
		// Budgeted: over-budget decides re-queue for next tick.
		constexpr float kBudgetMs = 8.0f;
		using Clock = std::chrono::steady_clock;
		auto phaseStart = Clock::now();

		auto ready = m_decisionQueue.drainReady(8);
		m_lastDecidesRun = 0;
		for (auto& [eid, last] : ready) {
			float elapsedMs = std::chrono::duration<float, std::milli>(
				Clock::now() - phaseStart).count();
			if (elapsedMs > kBudgetMs) {
				m_decisionQueue.enqueue(eid, std::move(last));
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
				m_decisionQueue.enqueue(eid, lastOutcome);
				return;
			}
		}

		// THREADING: snapshot on main thread; worker reads immutably.
		DecideRequest req;
		req.eid        = eid;
		req.generation = ++m_decideGen[eid];
		req.handle     = state.handle;
		req.self       = snapshotEntity(entity);
		req.nearby     = gatherNearbyFromServer(entity);
		req.worldTime  = m_server.worldTime();
		req.dt         = 0.25f;
		req.lastOutcome = lastOutcome;

		// THREADING: closures run on worker thread; stale block reads are visual-only races.
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

		// Bypasses the 64-block nearby cache for world-wide lookups.
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

		// Flowers/moss/… via annotationsForChunk.
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

	void drainWorkerResults() {
		DecideResult r;
		while (m_decideWorker.tryPop(r)) {
			// Discard results superseded by newer decide (interrupt etc).
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

	// Steps evaluated from observable world state; only action-internal durations use timers.
	static constexpr float kArriveEps       = 1.5f;
	static constexpr float kStillEps        = 0.1f;
	static constexpr float kStuckSeconds    = 2.0f;
	static constexpr float kReachHarvest    = 3.0f;
	static constexpr float kReachAttack     = 2.5f;
	// Idle-hold: stand-still Moves would thrash decide() at 60 Hz otherwise.
	static constexpr float kIdleHoldSeconds = 1.0f;
	static constexpr float kPeriodicRethinkSec = 1.0f;

	void phaseExecute(float dt) {
		for (auto& [eid, state] : m_agents) {
			// Drain override pause → flush deferred outcome to Python decide().
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

			// Skip while pause armed — that pause is the "sit still" promise.
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

		// Latch idle-hold on first eval if target == current pos.
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
			float horiz = std::sqrt(entity.velocity.x * entity.velocity.x +
			                        entity.velocity.z * entity.velocity.z);
			if (horiz < kStillEps) {
				watch.stillAccum += 1.0f / 60.0f;
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
			return StepOutcome::Success;
		}
		if (target->hp() <= 0) return StepOutcome::Success;
		watch.prevTargetHP = target->hp();
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateRelocate(PlanStep& /*step*/, Entity& /*entity*/,
	                             AgentState::StepWatch& watch, float /*dt*/) {
		// First eval: InProgress (applyStep fires once); next: Success.
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
		// Zero velocity so drift doesn't overshoot target.
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
		// Mirror from/to: server value-conservation needs matching X→X.
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
			// TODO: melee damage Convert.
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

	void finishPlan(EntityId eid, AgentState& state, StepOutcome outcome,
	                const std::string& reason) {
		PlanStep::Type lastType = PlanStep::Move;
		if (!state.plan.empty())
			lastType = state.plan[std::min(state.stepIndex,
			                               (int)state.plan.size() - 1)].type;
		// Decide is async — zero velocity prevents drift past target during replan.
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
		// Stash until pause drains → enqueue as interrupt:player_override.
		if (state.overridePauseTimer > 0.0f) {
			next.reason = "interrupt:player_override";
			state.pendingOutcome = std::move(next);
			state.hasPendingOutcome = true;
			return;
		}
		if (!m_decisionQueue.hasPending(eid))
			m_decisionQueue.enqueue(eid, std::move(next));
	}

	// Interrupt plan on urgent world changes (HP drop, target gone).
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
	// Player click overrides controlled NPC: plan := single Move, arm 3.14s pause,
	// defer re-decide until expires.
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

	// Control-mode entry: park pause at ~forever, no flush.
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

	// Control-mode exit: drop pause, enqueue immediate decide().
	void resumeAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		AgentState& state = it->second;
		state.overridePauseTimer = 0.0f;
		state.hasPendingOutcome = false;
		if (!m_decisionQueue.hasPending(eid))
			m_decisionQueue.enqueue(eid);
	}

	void onInterrupt(EntityId eid, const std::string& reason) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		interruptPlan(eid, it->second, *e, reason);
	}

	void onWorldEvent(const std::string& kind, const std::string& /*payload*/) {
		std::string reason = kind;
		for (auto& [eid, state] : m_agents) {
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			interruptPlan(eid, state, *e, reason);
		}
	}
private:

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

	void sendMove(EntityId eid, glm::vec3 vel, const std::string& goal) {
		// Client-side prediction; yaw derived from velocity elsewhere.
		Entity* e = m_server.getEntity(eid);
		if (e) {
			e->velocity.x = vel.x;
			e->velocity.z = vel.z;
		}

		// Stuck probe: intent>0 but no displacement ≥1.5s → log once.
		if (e) {
			auto it = m_agents.find(eid);
			if (it != m_agents.end()) {
				auto& s = it->second;
				float intent = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				float moved  = glm::length(glm::vec2(e->position.x, e->position.z) -
				                           glm::vec2(s.lastSampledPos.x, s.lastSampledPos.z));
				constexpr float kIntentThresh = 0.2f;
				constexpr float kMoveThresh   = 0.05f;
				constexpr float kStuckWindow  = 1.5f;
				const float dt = 1.0f / 60.0f;

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

	// Straight-line: collision is the server's problem.
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

	ServerInterface& m_server;
	BehaviorStore& m_behaviors;
	std::unordered_map<EntityId, AgentState> m_agents;
	DecisionQueue m_decisionQueue;
	std::mt19937 m_rng;
	float m_time = 0;
	float m_autoPickupTimer = 0.0f;
	float m_discoveryTimer = 100.0f; // forces discovery on first tick
	int m_lastDecidesRun = 0;
	float m_diagLogAccum = 0.0f;

	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace civcraft

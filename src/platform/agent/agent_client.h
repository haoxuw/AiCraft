#pragma once

// AI orchestrator for NPCs owned by this client. Owns one Agent per entity;
// drives them through a four-phase tick:
//
//   1. discoverEntities   — register/unregister Agents as the world changes
//   2. phaseAutoPickup    — engine-level convenience (humanoid grab nearby items)
//   3. phaseDecide        — drain DecisionQueue, dispatch to background worker
//   4. drainWorkerResults — install completed Plans on the right Agent
//   5. phaseExecute       — Agent.tickPlan: evaluate + apply current step
//   6. scanInterrupts     — HP-drop / target-gone → priority re-decide
//
// Single-producer / single-consumer architecture:
//
//   * Plans are owned by Agent. AgentClient never reads/writes plan state
//     directly — it only routes external callbacks (override/pause/interrupt)
//     and per-tick events to the matching Agent.
//
//   * The DecisionQueue has exactly ONE producer (Agent::requestRedecide,
//     reached only via Agent's public API) and exactly ONE consumer
//     (phaseDecide).
//
//   * The DecideWorker has exactly ONE producer (phaseDecide) and exactly ONE
//     consumer (drainWorkerResults). Stale results are filtered by a
//     per-eid generation counter held here.

#include "net/server_interface.h"
#include "server/behavior_store.h"
#include "server/behavior.h"
#include "server/python_bridge.h"
#include "agent/agent.h"
#include "agent/decision_queue.h"
#include "agent/behavior_executor.h"  // gatherNearby (used elsewhere)
#include "agent/decide_worker.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

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
		scanInterrupts();
		auto t3 = Clock::now();
		auto ms = [](Clock::duration d) {
			return std::chrono::duration<float, std::milli>(d).count();
		};
		float total = ms(t3 - t0);
		if (total > 20.0f) {
			static int cnt = 0; cnt++;
			if (cnt <= 5 || cnt % 60 == 0)
				std::fprintf(stderr,
					"[Agent] slow tick %.1fms — discover=%.1f decide=%.1f exec=%.1f "
					"(decides=%d agents=%zu pending=%zu oldest=%.1fs)\n",
					total, ms(t1 - t0), ms(t2 - t1), ms(t3 - t2),
					m_lastDecidesRun, m_agents.size(),
					m_decisionQueue.pendingCount(),
					m_decisionQueue.oldestWaitSec(m_time));
		}
		// Starvation watchdog: oldest-waiter > 5s in a busy queue → flag once.
		float oldestWait = m_decisionQueue.oldestWaitSec(m_time);
		if (oldestWait > 5.0f) {
			m_starvationLogAccum += dt;
			if (m_starvationLogAccum >= 5.0f) {
				m_starvationLogAccum = 0.0f;
				std::fprintf(stderr,
					"[Agent] starvation: eid=%u waited %.1fs (queue: %zu pri / %zu norm)\n",
					(unsigned)m_decisionQueue.oldestWaiter(), oldestWait,
					m_decisionQueue.priorityCount(), m_decisionQueue.normalCount());
			}
		} else {
			m_starvationLogAccum = 0.0f;
		}
	}

	// ── Inspector / renderer hooks ───────────────────────────────────────

	const PlanViz* getPlanViz(EntityId id) const {
		auto it = m_agents.find(id);
		if (it == m_agents.end()) return nullptr;
		if (!it->second.hasPlan()) return nullptr;
		return &it->second.viz();
	}

	PlanProgress getPlanProgress(EntityId id) const {
		auto it = m_agents.find(id);
		if (it == m_agents.end()) return PlanProgress{};
		return it->second.progress();
	}

	template<typename Fn>
	void forEachAgent(Fn&& fn) const {
		for (auto& [eid, agent] : m_agents) fn(eid, agent.viz());
	}

	// ── External callbacks (network + UI) ────────────────────────────────

	// Player click on an owned NPC → single-step Move + obey-pause.
	void onOverride(EntityId eid, glm::vec3 goal) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.applyOverride(goal, *e);
	}

	void pauseAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.pause(*e);
	}

	void resumeAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		it->second.resume(m_decisionQueue, m_time);
	}

	void onInterrupt(EntityId eid, const std::string& reason) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		it->second.onInterrupt(reason, m_decisionQueue, m_time);
	}

	void onWorldEvent(const std::string& kind, const std::string& /*payload*/) {
		for (auto& [eid, agent] : m_agents) {
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			agent.onInterrupt(kind, m_decisionQueue, m_time);
		}
	}

private:
	// ── discoverEntities ─────────────────────────────────────────────────

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

			BehaviorHandle h = loadBehaviorForEntity(bid);
			auto [ait, _ins] = m_agents.emplace(
				e.id(), Agent(e.id(), bid, h));
			if (h >= 0) {
				ait->second.requestInitialDecide(m_decisionQueue, m_time);
				registered++;
			}
		});

		for (auto it = m_agents.begin(); it != m_agents.end(); ) {
			Entity* e = m_server.getEntity(it->first);
			if (!e || e->removed) {
				if (it->second.handle() >= 0)
					pythonBridge().unloadBehavior(it->second.handle());
				m_decisionQueue.cancel(it->first);
				m_decideGen.erase(it->first);
				it = m_agents.erase(it);
			} else {
				++it;
			}
		}

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

	// Surface "why no NPCs are moving" first when diagnosing busted singleplayer.
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

	// ── phaseAutoPickup ──────────────────────────────────────────────────

	void phaseAutoPickup() {
		m_autoPickupTimer += 1.0f / 60.0f;
		if (m_autoPickupTimer < 0.25f) return;
		m_autoPickupTimer = 0.0f;

		for (auto& [eid, agent] : m_agents) {
			Entity* actor = m_server.getEntity(eid);
			if (!actor || actor->removed || !actor->inventory) continue;
			if (!actor->def().hasTag("humanoid")) continue;  // chickens vs eggs
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

	// ── phaseDecide — sole consumer of the DecisionQueue ─────────────────

	void phaseDecide() {
		// Wall-clock budget: at most kBudgetMs spent dispatching decides per
		// tick. Anything that doesn't fit gets re-inserted at the FRONT of
		// its lane so it keeps its turn next tick (already won the draw,
		// only the clock lost). This is the starvation guarantee.
		constexpr float kBudgetMs = 8.0f;
		constexpr int   kMaxDrain = 8;
		using Clock = std::chrono::steady_clock;
		auto phaseStart = Clock::now();

		auto requests = m_decisionQueue.drain(kMaxDrain);
		m_lastDecidesRun = 0;

		for (auto& req : requests) {
			float elapsedMs = std::chrono::duration<float, std::milli>(
				Clock::now() - phaseStart).count();
			if (elapsedMs > kBudgetMs) {
				m_decisionQueue.reinsertFront(std::move(req));
				continue;
			}

			auto ait = m_agents.find(req.eid);
			if (ait == m_agents.end()) continue;

			Entity* e = m_server.getEntity(req.eid);
			if (!e || e->removed) continue;

			if (ait->second.handle() < 0) {
				BehaviorHandle h = loadBehaviorForEntity(ait->second.behaviorId());
				if (h < 0) {
					m_decisionQueue.reinsertFront(std::move(req));
					continue;
				}
				ait->second.setHandle(h);
			}

			m_lastDecidesRun++;
			ait->second.noteDecideFired(m_time);
			dispatchDecide(ait->second, *e, req.outcome);
		}
	}

	// Build the immutable decide request and push it to the worker thread.
	void dispatchDecide(Agent& agent, Entity& entity,
	                    const LastOutcome& lastOutcome) {
		DecideRequest req;
		req.eid         = agent.id();
		req.generation  = ++m_decideGen[agent.id()];
		req.handle      = agent.handle();
		req.self        = snapshotEntity(entity);
		req.nearby      = gatherNearbyFromServer(entity);
		req.worldTime   = m_server.worldTime();
		req.dt          = 0.25f;
		req.lastOutcome = lastOutcome;

		// THREADING: closures run on worker thread; stale block reads are
		// visual-only races (no chunk mutation outside the server-tick path).
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

		req.appearanceQuery = [srv](int x, int y, int z) -> int {
			auto& chunks = srv->chunks();
			ChunkPos cp = worldToChunk(x, y, z);
			auto* chunk = chunks.getChunkIfLoaded(cp);
			if (!chunk) return 0;
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			return (int)chunk->getAppearance(lx, ly, lz);
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

	// ── drainWorkerResults — sole consumer of DecideWorker outputs ───────

	void drainWorkerResults() {
		DecideResult r;
		while (m_decideWorker.tryPop(r)) {
			// Stale-result filter: an interrupt that fired between dispatch
			// and worker completion bumped the generation counter.
			auto git = m_decideGen.find(r.eid);
			if (git == m_decideGen.end() || git->second != r.generation)
				continue;

			auto ait = m_agents.find(r.eid);
			if (ait == m_agents.end()) continue;

			Entity* e = m_server.getEntity(r.eid);
			if (!e || e->removed) continue;

			if (!r.error.empty()) {
				ait->second.onDecideError(r.error, *e, m_decisionQueue, m_time);
				continue;
			}
			ait->second.onDecideResult(std::move(r.plan),
			                           std::move(r.goalText), *e);
		}
	}

	// ── phaseExecute — drives every Agent's plan one step ────────────────

	void phaseExecute(float dt) {
		for (auto& [eid, agent] : m_agents) {
			agent.tickPlan(dt, m_server, m_decisionQueue, m_time);
		}
	}

	// ── scanInterrupts — HP-drop / target-gone watchdog ──────────────────

	void scanInterrupts() {
		for (auto& [eid, agent] : m_agents) {
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			agent.scanForInterrupts(*e, m_decisionQueue, m_time);
		}
	}

	// ── helpers ──────────────────────────────────────────────────────────

	BehaviorHandle loadBehaviorForEntity(const std::string& behaviorId) {
		std::string src = m_behaviors.load(behaviorId);
		if (src.empty()) {
			std::printf("[AgentClient] No behavior source for '%s'\n",
				behaviorId.c_str());
			return -1;
		}
		std::string err;
		BehaviorHandle h = pythonBridge().loadBehavior(src, err);
		if (h < 0)
			std::printf("[AgentClient] Failed to load '%s': %s\n",
				behaviorId.c_str(), err.c_str());
		return h;
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

	// ── State ────────────────────────────────────────────────────────────
	ServerInterface& m_server;
	BehaviorStore&   m_behaviors;
	std::unordered_map<EntityId, Agent> m_agents;
	DecisionQueue    m_decisionQueue;
	std::mt19937     m_rng;
	float            m_time              = 0;
	float            m_autoPickupTimer   = 0.0f;
	float            m_discoveryTimer    = 100.0f;  // forces discovery on first tick
	int              m_lastDecidesRun    = 0;
	float            m_diagLogAccum      = 0.0f;
	float            m_starvationLogAccum = 0.0f;

	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace civcraft

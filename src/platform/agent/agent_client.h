#pragma once

// AI orchestrator for NPCs owned by this client. Owns one Agent per entity;
// drives them through a fixed-phase tick:
//
//   1. discoverEntities   — register/unregister Agents as the world changes
//   2. phaseAutoPickup    — engine-level convenience (humanoid grab nearby items)
//   3. phaseDecide        — round-robin scan of m_needy → dispatch to worker
//   4. drainWorkerResults — install completed Plans on the right Agent
//   5. phaseExecute       — Agent.executePlan: evaluate + apply current step
//   6. emitMovementSignals — threat_nearby signal fan-in
//   7. scanInterrupts     — tick timers, HP-drop, refresh membership
//
// Scheduling:
//
//   There is no queue. Every Agent exposes needsCompute() → {None,React,Decide}.
//   Membership in `m_needy` (std::set<EntityId>) is derived from that after
//   every mutating call. phaseDecide walks the set in eid order starting at
//   a persistent cursor, dispatching until the 8 ms / 8-dispatch budget is
//   consumed. Cursor rotation guarantees no eid is starved.
//
//   React wins over Decide per-agent (signals represent "right now" events).
//   Across agents, the first needy agent at the cursor is served first,
//   regardless of kind — this is the deliberate "no cross-entity priority"
//   call (see design review).
//
//   In-flight tracking: Agent carries m_reactInFlight / m_decideInFlight.
//   React may preempt an in-flight Decide (the stale Decide result is
//   dropped via the generation counter). Decide waits for both to clear.

#include "net/server_interface.h"
#include "server/behavior_store.h"
#include "agent/behavior.h"
#include "python/python_bridge.h"
#include "agent/agent.h"
#include "agent/decide_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace civcraft {

class AgentClient {
public:
	// Runtime knobs plumbed in from the process that owns the AgentClient
	// (usually the Vulkan game client at game_vk.cpp). Every field has a
	// production default; callers override via CLI flags in client/main.cpp.
	struct Config {
		// Minimum gap between back-to-back Decide dispatches for the SAME
		// agent. The user requirement: "can't re-trigger decide more frequent
		// than game tick Hz" — so 1/60 ≈ 0.017s is the hard floor. The
		// default (0.1s) adds headroom so normal replans don't saturate the
		// worker thread for every NPC.
		float decideBaseCooldownSec = 0.10f;

		// Ceiling for the exponential-backoff curve. After enough consecutive
		// Failed_* outcomes (see Agent::kFailStreakGiveUp) the state is
		// promoted to Failed_GaveUp anyway, so this cap really only matters
		// for streak values that haven't hit the threshold yet.
		float decideMaxCooldownSec  = 10.00f;

		// cooldown(streak) = min(base * pow(backoffBase, streak), max).
		// Backoff base of 2 doubles the wait each failure: 0.1→0.2→0.4→0.8→…
		// Set to 1.0 to disable exponential growth and use the floor always.
		float decideBackoffBase     = 2.00f;
	};

	// The pacer sits between needsCompute()=Decide and actual dispatch. One
	// entry per agent: the wall-clock time of its last Decide dispatch. The
	// next dispatch is gated on `now - lastDispatch >= cooldown(failStreak)`,
	// where cooldown grows exponentially with the agent's current fail-streak.
	// This is orthogonal to m_decideBackoff (which only fires on Python
	// exceptions); DecidePacer paces *every* decide, healthy or failing.
	class DecidePacer {
	public:
		explicit DecidePacer(Config cfg) : m_cfg(cfg) {}

		// Returns true if the agent's last dispatch is still inside its
		// per-failStreak cooldown window. Callers should `continue` past the
		// agent when this returns true.
		bool inCooldown(EntityId eid, float now, int failStreak) const {
			auto it = m_lastDispatch.find(eid);
			if (it == m_lastDispatch.end()) return false;
			return (now - it->second) < cooldownFor(failStreak);
		}

		// Called after a successful dispatch. Pairs with inCooldown.
		void noteDispatch(EntityId eid, float now) {
			m_lastDispatch[eid] = now;
		}

		// Drop on agent removal so the map doesn't grow unbounded.
		void forget(EntityId eid) { m_lastDispatch.erase(eid); }

		// Exposed for diagnostics: the cooldown an agent with this streak
		// would face. Used by the repetitive-decide log line.
		float cooldownFor(int failStreak) const {
			float cd = m_cfg.decideBaseCooldownSec *
				std::pow(m_cfg.decideBackoffBase, (float)std::max(0, failStreak));
			return std::min(cd, m_cfg.decideMaxCooldownSec);
		}

	private:
		Config                              m_cfg;
		std::unordered_map<EntityId, float> m_lastDispatch;
	};

	AgentClient(ServerInterface& server, BehaviorStore& behaviors, Config cfg)
		: m_server(server), m_behaviors(behaviors),
		  m_cfg(cfg), m_decidePacer(cfg),
		  m_rng(std::random_device{}()) {
		std::printf("[Agent] AgentClient constructed — behavior store initialized=%d "
			"decideBase=%.3fs max=%.2fs backoffBase=%.2f pathfind_debug=%d\n",
			(int)m_behaviors.isInitialized(),
			m_cfg.decideBaseCooldownSec,
			m_cfg.decideMaxCooldownSec,
			m_cfg.decideBackoffBase,
			CIVCRAFT_PATHFINDING_DEBUG_ENABLED);
		std::fflush(stdout);
		m_decideWorker.start();
	}

	~AgentClient() {
		std::printf("[Agent] AgentClient destructed — had %zu agents\n", m_agents.size());
		std::fflush(stdout);
		m_decideWorker.stop();
	}

	// Loading-screen gate — when false, decide still runs (agents discover,
	// plans land, bookkeeping stays fresh) but phaseExecute / phaseAutoPickup
	// / emitMovementSignals are skipped so no entity actually moves or picks
	// anything up. The flag is flipped back on exactly once, the tick before
	// the loading screen hands off to Playing, so the world unfreezes in
	// lockstep with the first rendered gameplay frame.
	void setExecutorEnabled(bool on) { m_executorEnabled = on; }
	bool executorEnabled() const      { return m_executorEnabled; }

	void tick(float dt) {
		using Clock = std::chrono::steady_clock;
		m_time += dt;
		auto t0 = Clock::now();
		discoverEntities();
		if (m_executorEnabled) phaseAutoPickup();
		auto t1 = Clock::now();
		phaseDecide();
		drainWorkerResults();
		auto t2 = Clock::now();
		if (m_executorEnabled) {
			phaseExecute(dt);
			emitMovementSignals();
		}
		scanInterrupts(dt);
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
					"(decides=%d agents=%zu needy=%zu oldest=%.1fs)\n",
					total, ms(t1 - t0), ms(t2 - t1), ms(t3 - t2),
					m_lastDecidesRun, m_agents.size(),
					m_needy.size(), oldestWaitSec());
		}
		// Starvation watchdog: oldest-in-set > 5s → flag once per 5s.
		float oldestWait = oldestWaitSec();
		if (oldestWait > 5.0f) {
			m_starvationLogAccum += dt;
			if (m_starvationLogAccum >= 5.0f) {
				m_starvationLogAccum = 0.0f;
				std::fprintf(stderr,
					"[Agent] starvation: eid=%u waited %.1fs (needy=%zu)\n",
					(unsigned)oldestWaiter(), oldestWait, m_needy.size());
			}
		} else {
			m_starvationLogAccum = 0.0f;
		}

		m_lockPerfAccum += dt;
		if (m_lockPerfAccum >= 10.0f) {
			m_lockPerfAccum = 0.0f;
			ChunkLockStats s = m_server.chunks().snapshotLockStatsAndReset();
			if (s.writerCount || s.readerCount) {
				auto avg = [](uint64_t tot, uint64_t n) {
					return n ? (double)tot / (double)n / 1000.0 : 0.0;
				};
				std::fprintf(stderr,
					"[Agent] chunk-lock 10s: W=%llu (wait avg %.1fµs, hold avg %.1fµs) "
					"R=%llu (wait avg %.1fµs, hold avg %.1fµs) "
					"totalWriterWait=%.2fms totalReaderHold=%.2fms\n",
					(unsigned long long)s.writerCount,
					avg(s.writerWaitNs, s.writerCount),
					avg(s.writerHoldNs, s.writerCount),
					(unsigned long long)s.readerCount,
					avg(s.readerWaitNs, s.readerCount),
					avg(s.readerHoldNs, s.readerCount),
					s.writerWaitNs / 1e6,
					s.readerHoldNs / 1e6);
			}
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

	// Loading-screen readiness report. `discoveryRan` flips true after the
	// first real discoverEntities() pass (seat granted, entity snapshot seen);
	// `totalAgents` / `settledAgents` describe the owned NPC fleet's progress
	// through its first decide. Written data-driven so the loading gate can
	// render a live count without touching AgentClient internals.
	struct InitProgress {
		bool discoveryRan  = false;
		int  totalAgents   = 0;
		int  settledAgents = 0;
	};
	InitProgress initProgress() const {
		InitProgress ip;
		ip.discoveryRan = m_discoveryRanOnce;
		ip.totalAgents  = (int)m_agents.size();
		for (const auto& [eid, agent] : m_agents)
			if (agent.firstDecideDone()) ip.settledAgents++;
		return ip;
	}

	// ── External callbacks (network + UI) ────────────────────────────────
	void onOverride(EntityId eid, glm::vec3 goal) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.applyOverride(goal, *e);
		updateMembership(eid);
	}

	// RTS wheel slice commit → install a kind-specific Plan on the agent.
	// Clears any prior plan (including a forever-pause set by pauseAgent) and
	// runs the given Plan end-to-end; when it finishes, decide() takes over.
	void pushPlanOverride(EntityId eid, Plan plan, std::string goalText) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.applyPlanOverride(std::move(plan), std::move(goalText), *e);
		updateMembership(eid);
	}

	// Shift-queue variant: append Plan steps to the end of the active plan
	// without interrupting the step in flight. Falls back to replace semantics
	// when no plan is active.
	void appendPlanOverride(EntityId eid, Plan plan, std::string goalText) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.appendPlanOverride(std::move(plan), std::move(goalText), *e);
		updateMembership(eid);
	}

	void pauseAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		Entity* e = m_server.getEntity(eid);
		if (!e) return;
		it->second.pause(*e);
		updateMembership(eid);
	}

	void resumeAgent(EntityId eid) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		it->second.resume();
		updateMembership(eid);
	}

	void onInterrupt(EntityId eid, const std::string& reason) {
		auto it = m_agents.find(eid);
		if (it == m_agents.end()) return;
		it->second.onInterrupt(reason);
		updateMembership(eid);
	}

	void onWorldEvent(const std::string& kind, const std::string& /*payload*/) {
		for (auto& [eid, agent] : m_agents) {
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			agent.onWorldEvent(kind);
			updateMembership(eid);
		}
	}

private:
	// ── Membership: m_needy ⇔ (agent.needsCompute() != None) ─────────────
	void updateMembership(EntityId eid) {
		auto ait = m_agents.find(eid);
		if (ait == m_agents.end()) {
			m_needy.erase(eid);
			m_enteredAt.erase(eid);
			return;
		}
		bool needs = ait->second.needsCompute() != Agent::ComputeKind::None;
		if (needs) {
			if (m_needy.insert(eid).second) m_enteredAt[eid] = m_time;
		} else {
			if (m_needy.erase(eid)) m_enteredAt.erase(eid);
		}
	}

	float oldestWaitSec() const {
		if (m_enteredAt.empty()) return 0.0f;
		float oldest = m_time;
		for (auto& [eid, t] : m_enteredAt) if (t < oldest) oldest = t;
		return m_time - oldest;
	}

	EntityId oldestWaiter() const {
		EntityId best = ENTITY_NONE;
		float oldest = m_time;
		for (auto& [eid, t] : m_enteredAt)
			if (t < oldest) { oldest = t; best = eid; }
		return best;
	}

	// ── discoverEntities ─────────────────────────────────────────────────
	// Register new Living entities owned by our seat; drop agents whose
	// entity vanished or changed hands. No routine log — only warns on
	// orphaned Living (owner=0 or missing BehaviorId), which is a
	// spawn-attribution bug per docs/28_SEATS_AND_OWNERSHIP.md P6.
	void discoverEntities() {
		m_discoveryTimer += 1.0f / 60.0f;
		if (m_discoveryTimer < kDiscoverPeriodSec && !m_agents.empty()) return;
		m_discoveryTimer = 0;

		EntityId myEid  = m_server.localPlayerId();
		uint32_t mySeat = m_server.localSeatId();
		if (myEid == ENTITY_NONE) { warnRateLimited("handshake: no localPlayerId yet"); return; }
		if (mySeat == 0)          { warnRateLimited("handshake: no seat granted yet"); return; }
		m_discoveryRanOnce = true;

		std::unordered_map<std::string, int> orphanedOwner, orphanedBid;

		m_server.forEachEntity([&](Entity& e) {
			if (e.id() == myEid) return;
			// Hottest steady-state branch first: already adopted → single hash lookup.
			if (m_agents.count(e.id())) return;
			if (!e.def().isLiving() || e.removed) return;

			std::string bid = e.getProp<std::string>(Prop::BehaviorId, "");
			if (bid.empty())      { orphanedBid[e.typeId()]++; return; }
			int ownerSeat = e.getProp<int>(Prop::Owner, 0);
			if (ownerSeat == 0)   { orphanedOwner[e.typeId()]++; return; }
			if (ownerSeat != (int)mySeat) return;

			BehaviorHandle h = loadBehaviorForEntity(bid);
			auto [ait, _ins] = m_agents.emplace(e.id(), Agent(e.id(), bid, h));
			if (h >= 0) updateMembership(e.id());
		});

		// GC: drop agents whose entity is gone, removed, or no longer ours.
		// The owner-flip arm is what makes future taming/gifting correct —
		// the old seat's AgentClient releases here, the new seat's client
		// adopts on its next sweep.
		for (auto it = m_agents.begin(); it != m_agents.end(); ) {
			Entity* e = m_server.getEntity(it->first);
			bool drop = !e || e->removed
			         || e->getProp<int>(Prop::Owner, 0) != (int)mySeat;
			if (drop) {
				if (it->second.handle() >= 0)
					pythonBridge().unloadBehavior(it->second.handle());
				m_needy.erase(it->first);
				m_enteredAt.erase(it->first);
				m_decideGen.erase(it->first);
				m_decideBackoff.forget(it->first);
				m_decidePacer.forget(it->first);
				it = m_agents.erase(it);
			} else {
				++it;
			}
		}

		if (!orphanedOwner.empty() || !orphanedBid.empty())
			warnOrphans(orphanedOwner, orphanedBid);
	}

	void warnRateLimited(const std::string& what) {
		if (m_time - m_lastWarnTime < kWarnIntervalSec) return;
		m_lastWarnTime = m_time;
		std::printf("[Agent] %s\n", what.c_str());
		std::fflush(stdout);
	}

	void warnOrphans(const std::unordered_map<std::string,int>& byOwner,
	                 const std::unordered_map<std::string,int>& byBid) {
		if (m_time - m_lastWarnTime < kWarnIntervalSec) return;
		m_lastWarnTime = m_time;
		auto fmt = [](const std::unordered_map<std::string,int>& m) {
			std::vector<std::pair<std::string,int>> v(m.begin(), m.end());
			std::sort(v.begin(), v.end(), [](auto& a, auto& b){
				if (a.second != b.second) return a.second > b.second;
				return a.first < b.first;
			});
			std::string out;
			for (size_t i = 0; i < v.size(); i++) {
				if (i) out += ",";
				out += v[i].first + "×" + std::to_string(v[i].second);
			}
			return out;
		};
		std::printf("[Agent] orphaned Living (spawn-attribution bug)");
		if (!byOwner.empty()) std::printf(" owner=0[%s]", fmt(byOwner).c_str());
		if (!byBid.empty())   std::printf(" noBid[%s]",   fmt(byBid).c_str());
		std::printf("\n");
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

	// ── phaseDecide — round-robin over m_needy ───────────────────────────
	void phaseDecide() {
		constexpr float kBudgetMs = 8.0f;
		constexpr int   kMaxDispatch = 8;
		using Clock = std::chrono::steady_clock;
		auto phaseStart = Clock::now();
		m_lastDecidesRun = 0;

		if (m_needy.empty()) return;

		// Cursor resumes where we left off last tick. lower_bound on an
		// ordered set gives the first eid ≥ cursor; wrap to begin() if past end.
		auto it = m_needy.lower_bound(m_cursor);
		size_t visited = 0;
		const size_t N = m_needy.size();

		while (visited < N && m_lastDecidesRun < kMaxDispatch) {
			if (it == m_needy.end()) it = m_needy.begin();
			EntityId eid = *it;
			// Advance the iterator NOW — updateMembership during dispatch
			// may invalidate it via erase.
			++it;
			visited++;

			float elapsedMs = std::chrono::duration<float, std::milli>(
				Clock::now() - phaseStart).count();
			if (elapsedMs > kBudgetMs) { m_cursor = eid; return; }

			auto ait = m_agents.find(eid);
			if (ait == m_agents.end()) continue;
			Agent& agent = ait->second;

			Agent::ComputeKind wanted = agent.needsCompute();
			if (wanted == Agent::ComputeKind::None) continue;
			if (wanted == Agent::ComputeKind::React  && agent.reactInFlight()) continue;
			if (wanted == Agent::ComputeKind::Decide
			    && (agent.decideInFlight() || agent.reactInFlight())) continue;

			// Don't hammer a behavior that just raised — the exception will
			// repeat on identical inputs. Cooldown elapses → we retry.
			if (m_decideBackoff.inCooldown(eid, m_time)) continue;

			// General pacing gate: even a healthy agent can't re-dispatch
			// Decide faster than the configured floor; a failing agent
			// cools down exponentially with its streak so a bug that would
			// otherwise churn at game-tick Hz decays into a slow poll
			// (eventually bounded by kFailStreakGiveUp → Failed_GaveUp →
			// complain-at-town-center). React is "right now" by design and
			// bypasses the pacer — signals must land the same tick they fire.
			if (wanted == Agent::ComputeKind::Decide &&
			    m_decidePacer.inCooldown(eid, m_time, agent.failStreak())) {
				continue;
			}

			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;

			if (agent.handle() < 0) {
				BehaviorHandle h = loadBehaviorForEntity(agent.behaviorId());
				if (h < 0) continue;   // try again next sweep
				agent.setHandle(h);
			}

			m_lastDecidesRun++;
			dispatchWorker(agent, *e, wanted);
			agent.markDispatched(wanted, m_time);
			if (wanted == Agent::ComputeKind::Decide)
				m_decidePacer.noteDispatch(eid, m_time);
			updateMembership(eid);
		}

		// Completed the sweep; next tick resumes one past the last visited eid.
		if (it == m_needy.end()) it = m_needy.begin();
		m_cursor = (it == m_needy.end()) ? ENTITY_NONE : *it;
	}

	void dispatchWorker(Agent& agent, Entity& entity, Agent::ComputeKind kind) {
		DecideRequest req;
		req.kind = (kind == Agent::ComputeKind::React)
			? DecideRequest::Kind::React
			: DecideRequest::Kind::Decide;
		if (kind == Agent::ComputeKind::React) {
			req.signalKind    = agent.takeSignalKind();
			req.signalPayload = agent.takeSignalPayload();
		}
		req.eid         = agent.id();
		req.generation  = ++m_decideGen[agent.id()];
		req.handle      = agent.handle();
		req.self        = snapshotEntity(entity);
		req.nearby      = gatherNearbyFromServer(entity);
		req.worldTime   = m_server.worldTime();
		req.dt          = 0.25f;
		req.lastOutcome = agent.lastOutcome();

		// THREADING: closures run on the DecideWorker thread while the main
		// thread can be mutating LocalWorld (S_CHUNK_EVICT frees chunks, etc).
		// Hold a shared_lock on chunks.mutex() for the ENTIRE body of each
		// closure — a raw Chunk* survives the lock scope, no longer.
		ServerInterface* srv = &m_server;
		auto acquireShared = [](ChunkSource& cs) {
			struct Guard {
				ChunkSource& cs;
				std::shared_lock<std::shared_mutex> lk;
				std::chrono::steady_clock::time_point held;
				uint64_t waitNs;
				Guard(ChunkSource& c) : cs(c) {
					auto* m = cs.mutex();
					auto t0 = std::chrono::steady_clock::now();
					if (m) lk = std::shared_lock<std::shared_mutex>(*m);
					held = std::chrono::steady_clock::now();
					waitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
						held - t0).count();
				}
				~Guard() {
					auto t1 = std::chrono::steady_clock::now();
					uint64_t holdNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
						t1 - held).count();
					cs.recordReaderAcquire(waitNs, holdNs);
				}
			};
			return Guard(cs);
		};
		req.blockQuery = [srv, acquireShared](int x, int y, int z) -> std::string {
			auto& chunks = srv->chunks();
			auto guard = acquireShared(chunks);
			ChunkPos cp = worldToChunk(x, y, z);
			auto* chunk = chunks.getChunkIfLoaded(cp);
			if (!chunk) return "air";
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			BlockId bid = chunk->get(lx, ly, lz);
			return srv->blockRegistry().get(bid).string_id;
		};

		req.appearanceQuery = [srv, acquireShared](int x, int y, int z) -> int {
			auto& chunks = srv->chunks();
			auto guard = acquireShared(chunks);
			ChunkPos cp = worldToChunk(x, y, z);
			auto* chunk = chunks.getChunkIfLoaded(cp);
			if (!chunk) return 0;
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			return (int)chunk->getAppearance(lx, ly, lz);
		};

		req.scanBlocks = [srv, acquireShared](const std::string& typeId, glm::vec3 origin,
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
			// Acquire the shared_lock PER CHUNK rather than across the whole
			// scan — previously the lock was held for the full sweep (10-100ms
			// for a 3×3×3 region), starving S_CHUNK / S_CHUNK_EVICT writers
			// on the main thread. Per-chunk hold is ~50μs; writers slip in
			// between iterations and chunk lifetime is still safe because the
			// guard covers both getChunkIfLoaded() and the inner block walk.
			for (int cx = cxMin; cx <= cxMax; cx++)
			for (int cz = czMin; cz <= czMax; cz++)
			for (int cy = cyMin; cy <= cyMax; cy++) {
				auto guard = acquireShared(chunks);
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

	// ── drainWorkerResults ───────────────────────────────────────────────
	void drainWorkerResults() {
		DecideResult r;
		while (m_decideWorker.tryPop(r)) {
			auto ait = m_agents.find(r.eid);
			if (ait == m_agents.end()) continue;

			// Always release the in-flight slot first — the worker returns
			// exactly one result per dispatch.
			ait->second.clearInFlight(r.fromReact);

			// Stale: preempted by a newer dispatch. Flag already cleared.
			auto git = m_decideGen.find(r.eid);
			if (git == m_decideGen.end() || git->second != r.generation) {
				updateMembership(r.eid);
				continue;
			}

			Entity* e = m_server.getEntity(r.eid);
			if (!e || e->removed) { updateMembership(r.eid); continue; }

			if (!r.error.empty()) {
				m_decideBackoff.retryWithBackoff(r.eid, m_time,
					r.fromReact ? "react" : "decide", r.error);
				ait->second.onDecideError(r.error, *e);
			} else if (r.fromReact && r.reactNoOp) {
				// React produced no action: m_needsDecide untouched, next
				// visit will dispatch Decide (rule: react-None → decide-next).
			} else {
				m_decideBackoff.noteSuccess(r.eid);
				ait->second.onDecideResult(std::move(r.plan),
				                           std::move(r.goalText), *e);
			}
			updateMembership(r.eid);
		}
	}

	// ── phaseExecute — drives every Agent's plan one step ────────────────
	void phaseExecute(float dt) {
		for (auto& [eid, agent] : m_agents) {
			agent.executePlan(dt, m_server);
			updateMembership(eid);
		}
	}

	// ── scanInterrupts — timers + HP-drop + membership refresh ───────────
	void scanInterrupts(float dt) {
		for (auto& [eid, agent] : m_agents) {
			Entity* e = m_server.getEntity(eid);
			if (!e || e->removed) continue;
			agent.tickTimers(dt);
			agent.scanForInterrupts(*e);
			updateMembership(eid);
		}
	}

	// ── emitMovementSignals — engine-level threat detector ───────────────
	//
	// Event-driven: every Living whose horizontal velocity > kMoveSpeedThresh
	// inside kThreatAlertRadius of an agent of a different typeId sets the
	// dirty bit. O(A·N); react cooldown stops amplification.
	void emitMovementSignals() {
		if (m_agents.empty()) return;
		constexpr float kThreatAlertRadius = 16.0f;
		constexpr float kMoveSpeedThresh   = 0.05f;
		const float kAlertRadius2          = kThreatAlertRadius * kThreatAlertRadius;

		for (auto& [eid, agent] : m_agents) {
			Entity* self = m_server.getEntity(eid);
			if (!self || self->removed) continue;

			EntityId    bestId = ENTITY_NONE;
			float       bestD2 = kAlertRadius2;
			std::string bestType;

			m_server.forEachEntity([&](Entity& other) {
				if (other.id() == eid || other.removed) return;
				if (!other.def().isLiving()) return;
				if (other.typeId() == self->typeId()) return;
				float vh = std::sqrt(other.velocity.x * other.velocity.x +
				                     other.velocity.z * other.velocity.z);
				if (vh < kMoveSpeedThresh) return;
				glm::vec3 d = other.position - self->position;
				float d2 = glm::dot(d, d);
				if (d2 > bestD2) return;
				bestD2   = d2;
				bestId   = other.id();
				bestType = other.typeId();
			});
			if (bestId == ENTITY_NONE) continue;

			std::vector<std::pair<std::string, std::string>> payload;
			payload.emplace_back("source_id", std::to_string((unsigned)bestId));
			payload.emplace_back("source_type", bestType);
			agent.onSignal("threat_nearby", std::move(payload));
			updateMembership(eid);
		}
	}

	// ── helpers ──────────────────────────────────────────────────────────
	// Generic exponential-backoff gate for Python-side failures that would
	// otherwise spam once per decide sweep (missing behavior id, Python
	// exceptions in decide/react, etc.). Non-blocking failures: back off
	// 0.1s → 1s → 10s → ... capped at 1e10s (effectively give up). Optional
	// giveUpAfter bounds retries for cases where further attempts are
	// pointless (e.g. a behavior that hung the worker once already).
	template<typename Key>
	class RetryBackoff {
	public:
		// True during cooldown OR after giveUp — caller should skip work.
		bool inCooldown(const Key& k, float now) const {
			auto it = m_map.find(k);
			if (it == m_map.end()) return false;
			if (it->second.gaveUp) return true;
			return now < it->second.nextAttemptAt;
		}

		// Record a failure, schedule next attempt, log once. Returns true if
		// this failure exceeded giveUpAfter (caller may stop retrying for good).
		bool retryWithBackoff(const Key& k, float now,
		                      const char* label, const std::string& why,
		                      int giveUpAfter = 0) {
			auto& e = m_map[k];
			e.failures++;
			double delay = 0.1 * std::pow(10.0, e.failures - 1);
			if (delay > 1e10) delay = 1e10;
			e.nextAttemptAt = now + (float)delay;
			if (giveUpAfter > 0 && e.failures >= giveUpAfter) {
				e.gaveUp = true;
				std::printf("[AgentClient] %s failed for %s (%s); attempt %d — giving up\n",
					label, keyToStr(k).c_str(), why.c_str(), e.failures);
				return true;
			}
			std::printf("[AgentClient] %s failed for %s (%s); attempt %d, retry in %.1fs\n",
				label, keyToStr(k).c_str(), why.c_str(), e.failures, delay);
			return false;
		}

		void noteSuccess(const Key& k) { m_map.erase(k); }
		void forget(const Key& k)      { m_map.erase(k); }

	private:
		struct Entry { int failures = 0; float nextAttemptAt = 0.0f; bool gaveUp = false; };
		std::unordered_map<Key, Entry> m_map;

		static std::string keyToStr(const std::string& s) { return "'" + s + "'"; }
		static std::string keyToStr(EntityId e)           { return "eid#" + std::to_string((unsigned)e); }
	};

	BehaviorHandle loadBehaviorForEntity(const std::string& behaviorId) {
		if (m_loadBackoff.inCooldown(behaviorId, m_time)) return -1;

		std::string src = m_behaviors.load(behaviorId);
		if (src.empty()) {
			m_loadBackoff.retryWithBackoff(behaviorId, m_time, "load behavior", "no source");
			return -1;
		}
		std::string err;
		BehaviorHandle h = pythonBridge().loadBehavior(src, err);
		if (h < 0) {
			m_loadBackoff.retryWithBackoff(behaviorId, m_time, "load behavior", err);
			return h;
		}
		m_loadBackoff.noteSuccess(behaviorId);
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

	Config                    m_cfg;            // constructor copy of runtime knobs
	RetryBackoff<std::string> m_loadBackoff;    // behavior-id → load-failure backoff
	RetryBackoff<EntityId>    m_decideBackoff;  // entity-id  → decide/react runtime-error backoff
	DecidePacer               m_decidePacer;    // per-entity decide-dispatch cadence + fail-streak backoff

	// Scheduling: needy-set + rotating cursor.
	std::set<EntityId>                    m_needy;
	std::unordered_map<EntityId, float>   m_enteredAt;
	EntityId                              m_cursor = ENTITY_NONE;

	std::mt19937     m_rng;
	float            m_time              = 0;
	float            m_autoPickupTimer   = 0.0f;
	float            m_discoveryTimer    = 100.0f;
	// Flips true after the first discoverEntities() pass actually executes
	// (i.e. myEid + seat arrived). Loading-screen "agents settled" waits on
	// this — before the first discovery, m_agents may be empty for reasons
	// that don't mean the world is quiet (handshake still in flight).
	bool             m_discoveryRanOnce  = false;
	// Loading-screen executor gate. Defaults true so existing callers don't
	// change. Game flips this off during Connecting, and back on exactly one
	// tick before enterPlaying() hands control to the gameplay HUD.
	bool             m_executorEnabled   = true;
	int              m_lastDecidesRun    = 0;
	// Fires immediately on the first orphan/handshake warn (m_time starts at 0).
	float            m_lastWarnTime      = -1e6f;
	static constexpr float kDiscoverPeriodSec = 5.0f;
	static constexpr float kWarnIntervalSec   = 15.0f;
	float            m_starvationLogAccum = 0.0f;
	float            m_lockPerfAccum     = 0.0f;

	DecideWorker                           m_decideWorker;
	std::unordered_map<EntityId, uint32_t> m_decideGen;
};

} // namespace civcraft

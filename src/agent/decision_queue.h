#pragma once

/**
 * DecisionQueue — event set for triggering entity decide() calls.
 *
 * **Event-driven, not timer-driven.** Re-decides fire only on terminal
 * step outcomes (Success/Failed) or broadcast interrupts. No timestamps,
 * no polling.
 *
 * TODO(decide-loop): Step 4 of the plan replaces the current timestamp
 * min-heap (`schedule(eid, when)`) with a pure event map keyed by eid,
 * storing the `LastOutcome` that should be handed to the next decide()
 * call. Until then both APIs coexist: new `enqueue(eid, LastOutcome)`
 * below is unused, and callers keep using `schedule` / `scheduleNow`.
 *
 * Final shape (Step 4):
 *     class DecisionQueue {
 *         std::unordered_map<EntityId, LastOutcome> m_ready;
 *         void enqueue(EntityId eid, LastOutcome o) { m_ready[eid] = o; }
 *         std::vector<std::pair<EntityId, LastOutcome>> drain(int max);
 *     };
 * No heap, no chrono.
 */

#include "shared/types.h"
#include <queue>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

namespace modcraft {

using SteadyClock = std::chrono::steady_clock;
using SteadyTime  = SteadyClock::time_point;

// ── Event-driven decide types (TODO(decide-loop): used in Step 4+) ──

// Why the previous plan ended. Handed to the next decide() call so Python
// behaviors can branch on outcome rather than re-evaluating from scratch.
enum class StepOutcome {
	InProgress,   // executor shouldn't enqueue this — sentinel for evaluator
	Success,      // goal observably reached (or duration elapsed)
	Failed,       // observably unreachable / invalid
};

struct PlanStep;  // from server/behavior.h via agent/behavior_executor.h

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;                   // previous goal
	int          stepTypeRaw = 0;            // PlanStep::Type (kept as int to avoid include cycle)
	std::string  reason;                     // "stuck", "target_gone", "interrupt:proximity", ...
};

struct DecideEntry {
	SteadyTime timestamp;
	EntityId   entityId;
	uint32_t   generation;

	bool operator>(const DecideEntry& o) const {
		return timestamp > o.timestamp;
	}
};

class DecisionQueue {
public:
	// Schedule an entity's next decide() at the given absolute time.
	// Invalidates all prior entries for this entity (generation bump).
	void schedule(EntityId eid, SteadyTime when) {
		uint32_t gen = ++m_generation[eid];
		m_heap.push({when, eid, gen});
		m_scheduled.insert(eid);
	}

	// Schedule at now — used by active triggers.
	void scheduleNow(EntityId eid) {
		schedule(eid, SteadyClock::now());
	}

	// Pop up to maxPerTick entries whose timestamp <= now.
	// Returns entity IDs that should decide this tick.
	std::vector<EntityId> drain(int maxPerTick) {
		std::vector<EntityId> result;
		auto now = SteadyClock::now();
		while (!m_heap.empty() && (int)result.size() < maxPerTick) {
			const auto& top = m_heap.top();
			if (top.timestamp > now) break;
			DecideEntry e = top;
			m_heap.pop();
			// Lazy deletion: skip stale entries
			auto git = m_generation.find(e.entityId);
			if (git == m_generation.end() || git->second != e.generation)
				continue;
			m_scheduled.erase(e.entityId);
			result.push_back(e.entityId);
		}
		return result;
	}

	// O(1) check: does this entity have a valid pending entry?
	bool hasPending(EntityId eid) const {
		return m_scheduled.count(eid) > 0;
	}

	// Remove all state for an entity (on revoke / despawn).
	void remove(EntityId eid) {
		m_generation.erase(eid);
		m_scheduled.erase(eid);
		// Stale heap entries will be skipped by drain().
	}

	size_t pendingCount() const { return m_scheduled.size(); }

	// ── Event-driven API — TODO(decide-loop) Step 4 ───────────────────
	// Pseudocode:
	//     void enqueue(EntityId eid, LastOutcome o):
	//         m_ready[eid] = o       # latest-wins; newest interrupt trumps
	//                                # older terminal outcomes for same eid
	//     std::vector<std::pair<EntityId, LastOutcome>> drainReady(int max):
	//         pop up to `max` entries from m_ready, return them
	//
	// Generation bump still useful for stale-result filtering in the worker.
	// Data member (future): std::unordered_map<EntityId, LastOutcome> m_ready;

private:
	std::priority_queue<DecideEntry, std::vector<DecideEntry>,
	                    std::greater<DecideEntry>> m_heap;
	std::unordered_map<EntityId, uint32_t>  m_generation;
	std::unordered_set<EntityId>            m_scheduled;
};

} // namespace modcraft

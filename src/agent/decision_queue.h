#pragma once

/**
 * DecisionQueue — priority queue for scheduling entity decide() calls.
 *
 * Replaces the old fixed-rate 4 Hz polling timer. Each entity schedules
 * its next decide() at an absolute timestamp (now + duration returned by
 * the behavior). Active triggers (HP loss, proximity, time-of-day) push
 * entries at "now" to force immediate re-evaluation.
 *
 * Implementation: std::priority_queue (min-heap) with generation-based
 * lazy deletion. When a new entry is pushed for an entity, its generation
 * counter increments — stale entries are skipped on pop.
 *
 * A separate `m_scheduled` set tracks which entities have a valid pending
 * entry, enabling O(1) hasPending() checks for the periodic sweep.
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

private:
	std::priority_queue<DecideEntry, std::vector<DecideEntry>,
	                    std::greater<DecideEntry>> m_heap;
	std::unordered_map<EntityId, uint32_t>  m_generation;
	std::unordered_set<EntityId>            m_scheduled;
};

} // namespace modcraft

#pragma once

// Event-driven decide() trigger set. Latest-wins per eid.

#include "logic/types.h"
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

namespace civcraft {

// Handed to next decide() so Python can branch on outcome.
enum class StepOutcome {
	InProgress,   // evaluator sentinel, never enqueued
	Success,
	Failed,
};

struct PlanStep;

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;
	int          stepTypeRaw = 0;            // PlanStep::Type as int to avoid include cycle
	std::string  reason;                     // "stuck", "target_gone", "interrupt:...", …
};

class DecisionQueue {
public:
	void enqueue(EntityId eid, LastOutcome o = {}) {
		m_ready[eid] = std::move(o);
	}

	// Order is unspecified.
	std::vector<std::pair<EntityId, LastOutcome>> drainReady(int maxPerTick) {
		std::vector<std::pair<EntityId, LastOutcome>> result;
		result.reserve(std::min((int)m_ready.size(), maxPerTick));
		auto it = m_ready.begin();
		while (it != m_ready.end() && (int)result.size() < maxPerTick) {
			result.emplace_back(it->first, std::move(it->second));
			it = m_ready.erase(it);
		}
		return result;
	}

	bool hasPending(EntityId eid) const { return m_ready.count(eid) > 0; }

	void remove(EntityId eid) { m_ready.erase(eid); }

	size_t pendingCount() const { return m_ready.size(); }

private:
	std::unordered_map<EntityId, LastOutcome> m_ready;
};

} // namespace civcraft

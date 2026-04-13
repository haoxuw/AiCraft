#pragma once

/**
 * DecisionQueue — event set for triggering entity decide() calls.
 *
 * Event-driven, not timer-driven. Re-decides fire only on terminal
 * step outcomes (Success/Failed) or broadcast interrupts. No timestamps,
 * no polling.
 *
 * Latest-wins semantics: if enqueue() is called twice for the same eid
 * before drain, the newer LastOutcome wins (interrupts are more recent
 * than older terminal outcomes).
 */

#include "shared/types.h"
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

namespace modcraft {

// Why the previous plan ended. Handed to the next decide() call so Python
// behaviors can branch on outcome rather than re-evaluating from scratch.
enum class StepOutcome {
	InProgress,   // executor shouldn't enqueue this — sentinel for evaluator
	Success,      // goal observably reached (or duration elapsed)
	Failed,       // observably unreachable / invalid
};

struct PlanStep;  // from server/behavior.h

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;                   // previous goal
	int          stepTypeRaw = 0;            // PlanStep::Type (kept as int to avoid include cycle)
	std::string  reason;                     // "stuck", "target_gone", "interrupt:proximity", ...
};

class DecisionQueue {
public:
	// Enqueue an entity for decide() with the outcome of its previous plan.
	// Latest-wins: newer entries overwrite older ones for the same eid.
	void enqueue(EntityId eid, LastOutcome o = {}) {
		m_ready[eid] = std::move(o);
	}

	// Pop up to maxPerTick entries. Order is unspecified.
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

} // namespace modcraft

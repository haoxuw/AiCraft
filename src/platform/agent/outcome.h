#pragma once

// Outcome types shared by Agent (producer) and DecideWorker (consumer).
// Split out so neither has to include the other.

#include <string>
#include <utility>

namespace civcraft {

// Handed to next decide() so Python can branch on outcome.
enum class StepOutcome {
	InProgress,   // evaluator sentinel, never published
	Success,
	Failed,
};

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;
	int          stepTypeRaw = 0;            // PlanStep::Type as int to avoid include cycle
	std::string  reason;                     // "stuck", "target_gone", "interrupt:…", "discovery", …
};

} // namespace civcraft

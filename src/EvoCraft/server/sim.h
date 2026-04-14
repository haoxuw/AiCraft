// EvoCraft simulation — M1 is a pure heartbeat: one tick per second.
//
// Later milestones extend this to:
//   M3: cell physics at 30Hz
//   M4: call into pybind11 species.decide_batch() per tick
//   M5: body parts + inter-cell collisions
//
// The contract `bool advance(float dt) → did a tick boundary cross?` is
// stable across milestones so NetServer can always just ask "should I
// broadcast now?".

#pragma once

#include <cstdint>

namespace evocraft {

class Sim {
public:
	bool advance(float dt) {
		accumulator_ += dt;
		simTime_     += dt;
		if (accumulator_ >= kTickInterval) {
			accumulator_ -= kTickInterval;
			++tick_;
			return true;
		}
		return false;
	}

	uint64_t tick()    const { return tick_; }
	float    simTime() const { return simTime_; }

private:
	// 30Hz sim step (M3). Broadcast cadence is controlled separately in
	// main.cpp — typically every 2nd tick → 15Hz on the wire.
	static constexpr float kTickInterval = 1.0f / 30.0f;

	float    accumulator_ = 0.f;
	float    simTime_     = 0.f;
	uint64_t tick_        = 0;
};

} // namespace evocraft

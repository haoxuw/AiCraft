#pragma once

// MoveStuck logging → /tmp/civcraft_entity_<id>.log. Per-entity cooldown
// shared across detection sites (clamp/stuck/snap) prevents log flooding.

#include "logic/types.h"
#include "entity_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace civcraft {

// Cross-site: a Clamp log silences an Agent-Stuck log for the same eid during the window.
constexpr double kMoveStuckCooldownSec = 600.0;

inline bool moveStuckShouldLog(EntityId eid) {
	static std::mutex mu;
	static std::unordered_map<EntityId, double> last;
	std::lock_guard<std::mutex> lk(mu);
	double now = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	auto it = last.find(eid);
	if (it != last.end() && (now - it->second) < kMoveStuckCooldownSec) return false;
	last[eid] = now;
	return true;
}

// Writes: [MoveStuck:<tag>] reason="..." <detail>
inline void logMoveStuck(EntityId eid, const char* tag,
                         const char* reason, const char* detail) {
	if (!moveStuckShouldLog(eid)) return;
	entityLog(eid, "[MoveStuck:%s] reason=\"%s\" %s",
		tag, reason, detail ? detail : "");
}

} // namespace civcraft

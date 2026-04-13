#pragma once

// MoveStuck logging — per-entity, written to /tmp/modcraft_entity_<id>.log.
//
// Many detection sites (server collision clamp, agent stuck, client/server
// snap) can fire for the same entity within the same second. Routing each
// entity's diagnostics into its own file keeps the main game log clean
// while preserving full detail when you want to investigate one eid. The
// per-entity cooldown still applies so a single flailing entity doesn't
// overrun its own file either.

#include "types.h"
#include "entity_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace modcraft {

// Per-entity cooldown. First call for an entity within the window logs;
// subsequent calls are dropped. Cross-site: a Clamp log silences an
// Agent-Stuck log for the same entity for the window duration.
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

// Caller supplies site tag ("Clamp", "Agent-Stuck", ...), a short reason,
// and preformatted detail. Writes one line to the per-entity log:
//   [MoveStuck:<tag>] reason="..." <detail>
inline void logMoveStuck(EntityId eid, const char* tag,
                         const char* reason, const char* detail) {
	if (!moveStuckShouldLog(eid)) return;
	entityLog(eid, "[MoveStuck:%s] reason=\"%s\" %s",
		tag, reason, detail ? detail : "");
}

} // namespace modcraft

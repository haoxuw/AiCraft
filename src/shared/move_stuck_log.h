#pragma once

#include "types.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace modcraft {

// Per-entity cooldown for [MoveStuck] diagnostic logs. Many detection sites
// (server collision clamp, agent stuck, client/server snap) can all fire for
// the same entity within the same second; without this gate the console is
// unreadable. First call for an entity logs; subsequent calls within
// kMoveStuckCooldownSec are dropped. Cross-site: a Clamp log silences an
// Agent-Stuck log for the same entity for 10 min.
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

// Caller supplies site tag ("Clamp", "Agent-Stuck", ...), a short reason
// string, and a preformatted detail body. Emits a single line:
//   [MoveStuck:<tag>] eid=# reason="..." <detail>
inline void logMoveStuck(EntityId eid, const char* tag,
                         const char* reason, const char* detail) {
	if (!moveStuckShouldLog(eid)) return;
	fprintf(stderr, "[MoveStuck:%s] eid=%u reason=\"%s\" %s\n",
		tag, eid, reason, detail ? detail : "");
	fflush(stderr);
}

} // namespace modcraft

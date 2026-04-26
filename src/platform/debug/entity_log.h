#pragma once

// Per-entity log at /tmp/solarium_entity_<id>.log. Line-buffered for `tail -f`.
// Single source of truth — Python behaviors call solarium_engine.entity_log()
// which routes here via python_bridge.cpp. Shell callers (smoke scripts,
// `make game`) clear the files before launch; we never truncate at the API
// level.
//
// Aggregation: identical consecutive messages from a tight loop (e.g. a 10 Hz
// navigator tick logging the same "walking target=…" every frame) are folded
// into one summary row:
//     [HH:MM:SS..HH:MM:SS xN] msg
// The first occurrence prints immediately so a `tail -f` shows progress; the
// summary line is emitted when the message changes (or at shutdown via
// entityLogFlushAll()).

#include "logic/types.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

namespace detail {

struct EntityLogState {
	std::FILE* file        = nullptr;
	std::string pendingMsg;        // Last written message (awaiting more of the same).
	int         pendingCount = 0;   // >=1 when pendingMsg is valid.
	char        firstTs[16]   = "";
	char        lastTs[16]    = "";
};

inline std::mutex& entityLogMutex() {
	static std::mutex mu;
	return mu;
}

inline std::unordered_map<EntityId, EntityLogState>& entityLogStates() {
	static std::unordered_map<EntityId, EntityLogState> m;
	return m;
}

inline EntityLogState& openState(EntityId eid) {
	auto& states = entityLogStates();
	auto  it     = states.find(eid);
	if (it != states.end()) return it->second;

	EntityLogState s{};
	char path[128];
	std::snprintf(path, sizeof(path), "/tmp/solarium_entity_%u.log", eid);
	s.file = std::fopen(path, "a");    // O_APPEND: kernel-atomic writes.
	if (s.file) std::setvbuf(s.file, nullptr, _IOLBF, 0);
	return states.emplace(eid, s).first->second;
}

// Caller holds entityLogMutex().
inline void flushPending(EntityLogState& s) {
	if (s.pendingCount <= 1 || !s.file) {
		s.pendingCount = 0;
		s.pendingMsg.clear();
		return;
	}
	std::fprintf(s.file, "[%s..%s x%d] %s\n",
	             s.firstTs, s.lastTs, s.pendingCount, s.pendingMsg.c_str());
	s.pendingCount = 0;
	s.pendingMsg.clear();
}

} // namespace detail

// Back-compat accessor for call sites that want the raw FILE* (diagnostic
// dumps that bypass aggregation). Prefer entityLog() for normal use.
inline std::FILE* entityLogFile(EntityId eid) {
	std::lock_guard<std::mutex> lk(detail::entityLogMutex());
	return detail::openState(eid).file;
}

inline void entityLog(EntityId eid, const char* fmt, ...) {
	std::time_t t = std::time(nullptr);
	std::tm lt{};
	localtime_r(&t, &lt);
	char ts[16];
	std::strftime(ts, sizeof(ts), "%H:%M:%S", &lt);

	char msg[640];
	va_list ap;
	va_start(ap, fmt);
	int m = std::vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (m < 0) return;

	std::lock_guard<std::mutex> lk(detail::entityLogMutex());
	auto& s = detail::openState(eid);
	if (!s.file) return;

	// Aggregate identical consecutive messages: bump the counter and extend
	// the timestamp range instead of writing another copy to disk.
	if (s.pendingCount > 0 && s.pendingMsg == msg) {
		s.pendingCount++;
		std::snprintf(s.lastTs, sizeof(s.lastTs), "%s", ts);
		return;
	}

	// Different message — flush the prior aggregate (if it repeated), then
	// print the new line once and start buffering it.
	detail::flushPending(s);
	std::fprintf(s.file, "[%s] %s\n", ts, msg);
	s.pendingMsg = msg;
	s.pendingCount = 1;
	std::snprintf(s.firstTs, sizeof(s.firstTs), "%s", ts);
	std::snprintf(s.lastTs,  sizeof(s.lastTs),  "%s", ts);
}

// Call on clean shutdown so any buffered "[first..last xN]" summary reaches
// disk. Best-effort: tests and the game-loop exit paths drive this.
inline void entityLogFlushAll() {
	std::lock_guard<std::mutex> lk(detail::entityLogMutex());
	for (auto& kv : detail::entityLogStates()) {
		detail::flushPending(kv.second);
		if (kv.second.file) std::fflush(kv.second.file);
	}
}

// Return absolute paths of every per-entity log file this process has opened.
// `--debug-behavior` dumps this list at exit so the caller knows where to grep.
inline std::vector<std::string> entityLogProducedFiles() {
	std::lock_guard<std::mutex> lk(detail::entityLogMutex());
	std::vector<EntityId> ids;
	ids.reserve(detail::entityLogStates().size());
	for (auto& kv : detail::entityLogStates()) ids.push_back(kv.first);
	std::sort(ids.begin(), ids.end());
	std::vector<std::string> out;
	out.reserve(ids.size());
	for (EntityId e : ids) {
		char path[128];
		std::snprintf(path, sizeof(path), "/tmp/solarium_entity_%u.log", (unsigned)e);
		out.emplace_back(path);
	}
	return out;
}

} // namespace solarium

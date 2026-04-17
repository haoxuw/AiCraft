#pragma once

// Per-entity rolling log at /tmp/civcraft_entity_<id>.log.
//
// Mirrors python/entity_log.py — one line per event, buffered by-line so
// `tail -f` works in real time. Intended for per-entity diagnostics that
// would flood the main log if aggregated (MoveStuck:Agent-Stuck, agent
// decision tracing, etc.). Opens lazily on first write, overwrites any
// prior file for that entity on first use in the process. Truncation is
// per-process, not per-session — good enough for manual debugging.

#include "logic/types.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

namespace civcraft {

inline std::FILE* entityLogFile(EntityId eid) {
	static std::mutex mu;
	static std::unordered_map<EntityId, std::FILE*> files;
	std::lock_guard<std::mutex> lk(mu);
	auto it = files.find(eid);
	if (it != files.end()) return it->second;

	char path[128];
	std::snprintf(path, sizeof(path), "/tmp/civcraft_entity_%u.log", eid);
	std::FILE* f = std::fopen(path, "w");   // truncate on first open per process
	if (!f) return nullptr;
	std::setvbuf(f, nullptr, _IOLBF, 0);    // line-buffered so tail -f works
	files[eid] = f;
	return f;
}

inline void entityLog(EntityId eid, const char* fmt, ...) {
	std::FILE* f = entityLogFile(eid);
	if (!f) return;

	std::time_t t = std::time(nullptr);
	std::tm lt{};
	localtime_r(&t, &lt);
	char ts[16];
	std::strftime(ts, sizeof(ts), "%H:%M:%S", &lt);

	std::fprintf(f, "[%s] ", ts);
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(f, fmt, ap);
	va_end(ap);
	std::fputc('\n', f);
}

} // namespace civcraft

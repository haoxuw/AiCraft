#pragma once

// Per-entity log at /tmp/civcraft_entity_<id>.log. Line-buffered for `tail -f`.
// Truncates on first open per process (not per-session).
// Mirrors python/entity_log.py.

#include "logic/types.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

namespace civcraft {

// Shared lock protects both the file-handle map AND the actual writes: C++
// builds the whole line in a stack buffer then does ONE fwrite (atomic w.r.t.
// other C++ threads). Files are opened O_APPEND ("a" mode) so that separate
// FDs — e.g. Python's entity_log.log() pointing at the same path — can't
// clobber each other's offsets. We do NOT unlink/truncate here: Python's
// decide() opens the file during the first agent tick, which often happens
// *before* C++ navigateApproach logs — a C++ unlink would orphan Python's
// FD to a zombie inode and silently drop every subsequent Python log line.
// Shell callers (smoke scripts, `make game`) clear /tmp/civcraft_entity_*.log
// before launch.
inline std::FILE* entityLogFile(EntityId eid) {
	static std::mutex mu;
	static std::unordered_map<EntityId, std::FILE*> files;
	std::lock_guard<std::mutex> lk(mu);
	auto it = files.find(eid);
	if (it != files.end()) return it->second;

	char path[128];
	std::snprintf(path, sizeof(path), "/tmp/civcraft_entity_%u.log", eid);
	std::FILE* f = std::fopen(path, "a");    // O_APPEND: kernel-atomic writes
	if (!f) return nullptr;
	std::setvbuf(f, nullptr, _IOLBF, 0);
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

	char line[768];
	int  n = std::snprintf(line, sizeof(line), "[%s] ", ts);
	if (n < 0 || (size_t)n >= sizeof(line)) return;

	va_list ap;
	va_start(ap, fmt);
	int m = std::vsnprintf(line + n, sizeof(line) - n, fmt, ap);
	va_end(ap);
	if (m < 0) return;

	size_t len = (size_t)n + (size_t)m;
	if (len >= sizeof(line) - 1) len = sizeof(line) - 2;
	line[len]     = '\n';
	line[len + 1] = '\0';

	static std::mutex writeMu;
	std::lock_guard<std::mutex> lk(writeMu);
	std::fwrite(line, 1, len + 1, f);
}

} // namespace civcraft

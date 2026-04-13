#pragma once
/**
 * CrashLog — dump a structured report + abort when the game hits an
 * unrecoverable state (e.g. a client/server snap-back deadlock).
 *
 * Output: /tmp/modcraft_crash.log (overwritten each crash). The report is
 * also echoed to stderr and to the GameLogger so the in-memory ring
 * buffer preserves it up to the moment of abort.
 *
 * Typical use:
 *
 *     CrashLog cl("SnapDeadlock eid=42");
 *     cl.line("reason", "10 snap-backs in 10s");
 *     cl.line("entity", "id=%u type=%s", id, type);
 *     cl.kv("clientPos",  "(%.2f,%.2f,%.2f)", pos.x, pos.y, pos.z);
 *     cl.abort();   // writes file + std::abort()
 *
 * The class is header-only and has no dependencies beyond GameLogger so
 * it can be used from any translation unit.
 */

#include "client/game_logger.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace modcraft {

class CrashLog {
public:
	explicit CrashLog(const std::string& title)
		: m_title(title) {
		m_lines.reserve(32);
		line("==== MODCRAFT CRASH ====");
		line("title: %s", title.c_str());
	}

	// Printf-style free-form line.
	void line(const char* fmt, ...) {
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		m_lines.emplace_back(buf);
	}

	// "key = value" row with printf-style value.
	void kv(const char* key, const char* fmt, ...) {
		char val[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(val, sizeof(val), fmt, ap);
		va_end(ap);
		char row[640];
		snprintf(row, sizeof(row), "  %-14s = %s", key, val);
		m_lines.emplace_back(row);
	}

	// Begin a named section header.
	void section(const char* name) {
		char row[128];
		snprintf(row, sizeof(row), "-- %s --", name);
		m_lines.emplace_back(row);
	}

	// Write to /tmp/modcraft_crash.log + stderr + GameLogger, then abort.
	[[noreturn]] void abort() {
		std::string path =
			(std::filesystem::temp_directory_path() / "modcraft_crash.log").string();
		if (FILE* f = std::fopen(path.c_str(), "w")) {
			for (auto& l : m_lines) {
				std::fputs(l.c_str(), f);
				std::fputc('\n', f);
			}
			std::fclose(f);
		}
		std::fputs("\n", stderr);
		for (auto& l : m_lines) {
			std::fputs(l.c_str(), stderr);
			std::fputc('\n', stderr);
		}
		std::fflush(stderr);
		// Echo a single CRASH line into the game log so the in-memory
		// ring buffer preserves the context visible to the menu viewer.
		GameLogger::instance().emit("CRASH", "%s (see %s)",
			m_title.c_str(), path.c_str());
		std::abort();
	}

	// Write the report without aborting — for soft-crash diagnostics.
	void dump() {
		std::string path =
			(std::filesystem::temp_directory_path() / "modcraft_crash.log").string();
		if (FILE* f = std::fopen(path.c_str(), "w")) {
			for (auto& l : m_lines) {
				std::fputs(l.c_str(), f);
				std::fputc('\n', f);
			}
			std::fclose(f);
		}
	}

private:
	std::string m_title;
	std::vector<std::string> m_lines;
};

} // namespace modcraft

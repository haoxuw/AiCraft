#pragma once
// Structured crash report → /tmp/civcraft_crash.log + stderr + GameLogger, then abort.

#include "client/game_logger.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace civcraft {

class CrashLog {
public:
	explicit CrashLog(const std::string& title)
		: m_title(title) {
		m_lines.reserve(32);
		line("==== CIVCRAFT CRASH ====");
		line("title: %s", title.c_str());
	}

	void line(const char* fmt, ...) {
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		m_lines.emplace_back(buf);
	}

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

	void section(const char* name) {
		char row[128];
		snprintf(row, sizeof(row), "-- %s --", name);
		m_lines.emplace_back(row);
	}

	[[noreturn]] void abort() {
		std::string path =
			(std::filesystem::temp_directory_path() / "civcraft_crash.log").string();
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
		GameLogger::instance().emit("CRASH", "%s (see %s)",
			m_title.c_str(), path.c_str());
		std::abort();
	}

	// Soft-crash variant: writes report but does not abort.
	void dump() {
		std::string path =
			(std::filesystem::temp_directory_path() / "civcraft_crash.log").string();
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

} // namespace civcraft

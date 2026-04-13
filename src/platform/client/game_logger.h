#pragma once
// GameLogger — WoW-style event log.
// Tees every event to:
//   1. /tmp/modcraft_game.log (append, line-buffered; prior session → .log.prev)
//   2. in-memory ring buffer (for the Main Menu → Game Log viewer)
//   3. stdout when headless (--log-only)
//
// Event derivation lives elsewhere (game.cpp, network_server.h). This class
// is a pure sink — it only knows how to format + persist lines.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <ctime>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace modcraft {

class GameLogger {
public:
	static GameLogger& instance() {
		static GameLogger g;
		return g;
	}

	// Call once at process start. Rotates prior log to .prev, truncates new file.
	void init(bool echoStdout = false) {
		std::lock_guard<std::mutex> lk(m_mu);
		if (m_initialized) return;
		m_echoStdout = echoStdout;
		namespace fs = std::filesystem;
		fs::path tmp = fs::temp_directory_path();
		m_path = (tmp / "modcraft_game.log").string();
		fs::path prev = tmp / "modcraft_game.log.prev";
		std::error_code ec;
		if (fs::exists(m_path, ec)) {
			fs::remove(prev, ec);
			fs::rename(m_path, prev, ec);
		}
		m_file = std::fopen(m_path.c_str(), "w");
		if (m_file) setvbuf(m_file, nullptr, _IOLBF, 0);
		m_initialized = true;
		// Header so readers know which process this belongs to
		char hdr[128];
		snprintf(hdr, sizeof(hdr), "=== modcraft session pid=%d ===", (int)getpid());
		if (m_file) { std::fputs(hdr, m_file); std::fputc('\n', m_file); }
		if (m_echoStdout) { std::fputs(hdr, stdout); std::fputc('\n', stdout); }
	}

	void shutdown() {
		std::lock_guard<std::mutex> lk(m_mu);
		if (m_file) { std::fclose(m_file); m_file = nullptr; }
		m_initialized = false;
	}

	// Log a raw line (already formatted with clock-time prefix + category).
	// Prefer emit() which does the formatting.
	void write(const std::string& line) {
		std::lock_guard<std::mutex> lk(m_mu);
		pushLocked(line);
	}

	// Emit a categorized event:
	//   [HH:MM:SS] [CATEGORY] actor event...
	// Real wall-clock HH:MM:SS so external readers can correlate.
	void emit(const char* category, const char* fmt, ...) {
		char body[512];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(body, sizeof(body), fmt, ap);
		va_end(ap);

		std::time_t t = std::time(nullptr);
		std::tm lt{};
#ifdef _WIN32
		localtime_s(&lt, &t);
#else
		localtime_r(&t, &lt);
#endif
		char line[640];
		snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%s] %s",
			lt.tm_hour, lt.tm_min, lt.tm_sec, category, body);
		std::lock_guard<std::mutex> lk(m_mu);
		pushLocked(line);
	}

	// Ring-buffer accessors for the in-menu viewer.
	std::deque<std::string> snapshot() {
		std::lock_guard<std::mutex> lk(m_mu);
		return m_buf;
	}

	const std::string& path() const { return m_path; }

private:
	GameLogger() = default;
	~GameLogger() { shutdown(); }

	void pushLocked(const std::string& line) {
		m_buf.push_back(line);
		if (m_buf.size() > kMaxBuf) m_buf.pop_front();
		if (m_file) { std::fputs(line.c_str(), m_file); std::fputc('\n', m_file); }
		if (m_echoStdout) { std::fputs(line.c_str(), stdout); std::fputc('\n', stdout); }
	}

	static constexpr size_t kMaxBuf = 2000;

	std::mutex  m_mu;
	bool        m_initialized = false;
	bool        m_echoStdout  = false;
	std::FILE*  m_file        = nullptr;
	std::string m_path;
	std::deque<std::string> m_buf;
};

} // namespace modcraft

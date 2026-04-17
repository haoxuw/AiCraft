#pragma once
// GameLogger — WoW-style event log. Tees to /tmp/civcraft_game.log (line-buffered;
// prior → .log.prev), an in-memory ring buffer (menu viewer), and stdout when --log-only.
// Event derivation lives in game.cpp/network_server.h; this class is a pure sink.

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

namespace civcraft {

class GameLogger {
public:
	static GameLogger& instance() {
		static GameLogger g;
		return g;
	}

	// Call once at process start; rotates prior log to .prev and truncates.
	void init(bool echoStdout = false) {
		std::lock_guard<std::mutex> lk(m_mu);
		if (m_initialized) return;
		m_echoStdout = echoStdout;
		namespace fs = std::filesystem;
		fs::path tmp = fs::temp_directory_path();
		m_path = (tmp / "civcraft_game.log").string();
		fs::path prev = tmp / "civcraft_game.log.prev";
		std::error_code ec;
		if (fs::exists(m_path, ec)) {
			fs::remove(prev, ec);
			fs::rename(m_path, prev, ec);
		}
		m_file = std::fopen(m_path.c_str(), "w");
		if (m_file) setvbuf(m_file, nullptr, _IOLBF, 0);
		m_initialized = true;
		char hdr[128];
		snprintf(hdr, sizeof(hdr), "=== civcraft session pid=%d ===", (int)getpid());
		if (m_file) { std::fputs(hdr, m_file); std::fputc('\n', m_file); }
		if (m_echoStdout) { std::fputs(hdr, stdout); std::fputc('\n', stdout); }
	}

	void shutdown() {
		std::lock_guard<std::mutex> lk(m_mu);
		if (m_file) { std::fclose(m_file); m_file = nullptr; }
		m_initialized = false;
	}

	// Prefer emit() — this takes a pre-formatted line (incl. time prefix + category).
	void write(const std::string& line) {
		std::lock_guard<std::mutex> lk(m_mu);
		pushLocked(line);
	}

	// Emits: "[HH:MM:SS] [CATEGORY] actor event..." — real wall-clock for correlation.
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

} // namespace civcraft

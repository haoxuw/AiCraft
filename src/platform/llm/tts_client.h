#pragma once

// TtsClient — async synthesis facade over TtsSidecar's stdin/stdout pipes.
//
// Piper consumes one JSON line per utterance and emits a WAV path on its
// stdout. We own a worker thread that:
//   1. Drains a FIFO of synthesis requests.
//   2. Writes `{"text": "..."}\n` to the child's stdin.
//   3. Reads the child's stdout until it prints an absolute WAV path.
//   4. Fires onDone(ok, path) back on the worker thread.
//
// Callers (e.g. DialogPanel) typically wire onDone to AudioManager::playFile.
//
// The caller owns TtsSidecar; we only hold its file descriptors. If the
// sidecar dies mid-request, the worker surfaces ok=false and the client stays
// usable — subsequent requests will also fail but not crash.

#include "llm/tts_sidecar.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <unistd.h>

namespace civcraft::llm {

class TtsClient {
public:
	using OnDone = std::function<void(bool ok, std::string wavPath)>;

	// `sidecar` must outlive this client. We don't take ownership.
	explicit TtsClient(TtsSidecar* sidecar)
		: m_sidecar(sidecar) {
		m_thread = std::thread([this] { workerLoop(); });
	}

	~TtsClient() {
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_quit.store(true);
		}
		m_cv.notify_all();
		if (m_thread.joinable()) m_thread.join();
	}

	TtsClient(const TtsClient&) = delete;
	TtsClient& operator=(const TtsClient&) = delete;

	// Queue `text` for synthesis. onDone fires from the worker thread when
	// the WAV path is known. Empty text is a no-op (doesn't call onDone).
	void speak(std::string text, OnDone onDone) {
		// Trim + collapse whitespace so piper doesn't synthesize a silent file.
		std::string trimmed;
		trimmed.reserve(text.size());
		bool lastSpace = true;
		for (char c : text) {
			if (c == '\n' || c == '\r' || c == '\t') c = ' ';
			if (c == ' ' && lastSpace) continue;
			trimmed.push_back(c);
			lastSpace = (c == ' ');
		}
		while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
		if (trimmed.empty()) return;

		Request r;
		r.text   = std::move(trimmed);
		r.onDone = std::move(onDone);
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_queue.push_back(std::move(r));
		}
		m_cv.notify_one();
	}

private:
	struct Request {
		std::string text;
		OnDone      onDone;
	};

	// JSON-escape with the same rules LlmClient uses.
	static void jsonEscapeInto(std::string& out, const std::string& s) {
		for (char c : s) {
			switch (c) {
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n";  break;
				case '\r': out += "\\r";  break;
				case '\t': out += "\\t";  break;
				default:
					if ((unsigned char)c < 0x20) {
						char buf[8];
						std::snprintf(buf, sizeof(buf), "\\u%04x", c);
						out += buf;
					} else {
						out.push_back(c);
					}
			}
		}
	}

	static bool writeAll(int fd, const char* buf, size_t n) {
		size_t sent = 0;
		while (sent < n) {
			ssize_t w = ::write(fd, buf + sent, n - sent);
			if (w > 0) { sent += (size_t)w; continue; }
			if (w < 0 && errno == EINTR) continue;
			return false;
		}
		return true;
	}

	// Read one line from fd. Returns empty on EOF/error.
	static std::string readLine(int fd) {
		std::string out;
		char c;
		while (out.size() < 4096) {
			ssize_t n = ::read(fd, &c, 1);
			if (n <= 0) return out;
			if (c == '\n') return out;
			out.push_back(c);
		}
		return out;
	}

	void workerLoop() {
		for (;;) {
			Request req;
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				m_cv.wait(lk, [this] { return m_quit.load() || !m_queue.empty(); });
				if (m_quit.load()) return;
				req = std::move(m_queue.front());
				m_queue.pop_front();
			}

			std::string path, err;
			bool ok = runRequest(req, path, err);
			if (req.onDone) req.onDone(ok, ok ? path : err);
		}
	}

	bool runRequest(const Request& r, std::string& path, std::string& err) {
		if (!m_sidecar || !m_sidecar->running()) {
			err = "tts sidecar not running";
			return false;
		}
		int in  = m_sidecar->stdinFd();
		int out = m_sidecar->stdoutFd();
		if (in < 0 || out < 0) { err = "tts pipes closed"; return false; }

		// Build JSON request.
		std::string req = "{\"text\":\"";
		jsonEscapeInto(req, r.text);
		req += "\"}\n";
		if (!writeAll(in, req.data(), req.size())) {
			err = "pipe write failed";
			return false;
		}

		// Piper prints the output path on stdout when synthesis finishes.
		// Occasionally its own progress output shows up — skip until we see
		// a line that looks like a file path.
		for (int tries = 0; tries < 8; ++tries) {
			std::string line = readLine(out);
			// Strip trailing \r from Windows-ish newlines (shouldn't happen but safe).
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			if (line.empty()) { err = "piper closed stdout"; return false; }
			if (line.size() > 5 && line[0] == '/') {
				path = std::move(line);
				return true;
			}
			// Not a path — keep reading.
		}
		err = "no path from piper after 8 lines";
		return false;
	}

	TtsSidecar*             m_sidecar;
	std::thread             m_thread;
	std::atomic<bool>       m_quit{false};
	std::mutex              m_mtx;
	std::condition_variable m_cv;
	std::deque<Request>     m_queue;
};

} // namespace civcraft::llm

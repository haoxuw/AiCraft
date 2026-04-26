#pragma once

// WhisperClient — thin async transcription client for whisper.cpp's
// `whisper-server` (sibling of LlmClient, same architectural shape).
//
//   POST http://host:port/inference
//        multipart/form-data:  file=<audio.wav>
//                              response_format=json
//   ← 200 {"text": "transcribed utterance"}
//
// One worker thread drains a FIFO; each submission fires onDone(ok, text).
// No libcurl: raw POSIX socket + HTTP/1.1.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace solarium::llm {

class WhisperClient {
public:
	using OnDone = std::function<void(bool ok, std::string text)>;

	struct Config {
		std::string host = "127.0.0.1";
		int         port = 8081;
	};

	WhisperClient() = default;
	explicit WhisperClient(const Config& cfg);
	~WhisperClient();

	WhisperClient(const WhisperClient&) = delete;
	WhisperClient& operator=(const WhisperClient&) = delete;

	// Kick off transcription of wavBytes. onDone fires from the worker thread
	// once whisper-server replies (or the request fails).
	void transcribe(std::vector<uint8_t> wavBytes, OnDone onDone);

private:
	struct Request {
		std::vector<uint8_t> wav;
		OnDone onDone;
	};

	void workerLoop();
	bool runRequest(const Request& r, std::string* text, std::string* err);

	Config              m_cfg;
	std::thread         m_thread;
	std::atomic<bool>   m_quit{false};
	std::mutex          m_mtx;
	std::condition_variable m_cv;
	std::deque<Request> m_queue;
};

} // namespace solarium::llm

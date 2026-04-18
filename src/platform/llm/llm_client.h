#pragma once

// LlmClient — HTTP/SSE client to a locally-run `llama-server` sidecar.
//
// One worker thread processes a FIFO queue of chat-completion requests. Each
// request opens a blocking TCP socket, sends an HTTP/1.1 POST with
// stream=true, then parses text/event-stream responses, invoking the caller's
// token callback per delta. Callbacks fire on the worker thread — the caller
// is responsible for forwarding tokens to the main/render thread safely
// (LlmSession does this).
//
// Graceful-degradation contract: if the sidecar is unreachable or returns an
// error, onDone(false, msg) fires and onToken never does. The client stays
// live and accepts further requests, so a user who runs `make llm_server`
// mid-session can press Talk again without restarting the game.

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <cstdint>

namespace civcraft::llm {

struct ChatMessage {
	std::string role;    // "system" | "user" | "assistant"
	std::string content;
};

class LlmClient {
public:
	using TokenCallback = std::function<void(const std::string&)>;
	using DoneCallback  = std::function<void(bool ok, const std::string& err)>;

	LlmClient(std::string host, int port);
	~LlmClient();

	LlmClient(const LlmClient&) = delete;
	LlmClient& operator=(const LlmClient&) = delete;

	// Enqueue a streamed chat completion. Returns a request id used to cancel.
	// Callbacks run on the worker thread. onDone always fires exactly once.
	uint64_t chatStream(std::vector<ChatMessage> messages,
	                    float temperature, int maxTokens,
	                    TokenCallback onToken, DoneCallback onDone);

	// Abort an in-flight or queued request. Closes the socket so the HTTP
	// read loop exits; onDone fires with ok=false, err="cancelled".
	// No-op if the id isn't the active or queued request.
	void cancel(uint64_t requestId);

	// Blocking ping — opens a TCP connection, sends GET /health, checks for
	// HTTP 200. Used by startup probes. Returns false if the port isn't
	// listening or the reply isn't 200 within timeoutSec.
	bool health(float timeoutSec = 0.5f) const;

private:
	struct Request {
		uint64_t id;
		std::vector<ChatMessage> messages;
		float temperature;
		int maxTokens;
		TokenCallback onToken;
		DoneCallback  onDone;
	};

	void workerLoop();
	void runRequest(Request& req);

	std::string m_host;
	int         m_port;

	std::thread             m_worker;
	std::mutex              m_mtx;
	std::condition_variable m_cv;
	std::deque<Request>     m_queue;
	std::atomic<bool>       m_shutdown{false};

	// The request currently being processed by the worker. cancel() closes
	// m_activeFd so the socket read returns and the handler unwinds.
	std::atomic<uint64_t>   m_activeId{0};
	std::atomic<int>        m_activeFd{-1};

	std::atomic<uint64_t>   m_nextId{1};
};

} // namespace civcraft::llm

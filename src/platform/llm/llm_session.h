#pragma once

// LlmSession — per-chat-target conversation state.
//
// Owns: the system prompt, message history, and (while streaming) the
// partial-reply accumulator. The DialogPanel reads `streaming()` + `reply()`
// each frame to render live tokens as they arrive.
//
// Thread model: `send()` posts to LlmClient's worker and returns. Token +
// done callbacks mutate the session from the worker thread, guarded by
// m_mtx. Readers (render) take a snapshot via `snapshot()` — a short lock,
// no allocations held across the critical section.

#include "llm/llm_client.h"

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace civcraft::llm {

class LlmSession {
public:
	struct Snapshot {
		std::vector<ChatMessage> history; // committed turns (system + user + assistant)
		std::string              partial; // streaming assistant reply (may be empty)
		bool                     streaming = false;
		std::string              lastError;
	};

	LlmSession(LlmClient& client, std::string systemPrompt, float temperature, int maxTokens = 160)
		: m_client(client)
		, m_temperature(temperature)
		, m_maxTokens(maxTokens) {
		if (!systemPrompt.empty()) {
			m_history.push_back({"system", std::move(systemPrompt)});
		}
	}

	~LlmSession() {
		// Ensure the worker isn't still holding references to our mutex.
		uint64_t id;
		{ std::lock_guard<std::mutex> lk(m_mtx); id = m_activeId; }
		if (id) m_client.cancel(id);
	}

	LlmSession(const LlmSession&) = delete;
	LlmSession& operator=(const LlmSession&) = delete;

	// Append a user turn and start streaming the assistant reply.
	// Drops any in-flight reply first.
	void send(std::string userText) {
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			if (m_activeId) {
				// cancel() will fire onDone(false, "cancelled"); we want to
				// ignore that callback because we're starting a fresh turn.
				m_generation++;
				m_client.cancel(m_activeId);
				m_activeId = 0;
			}
			m_history.push_back({"user", std::move(userText)});
			m_partial.clear();
			m_streaming = true;
			m_lastError.clear();
		}

		uint64_t myGen;
		std::vector<ChatMessage> msgs;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			myGen = ++m_generation;
			msgs = m_history;
		}

		uint64_t id = m_client.chatStream(
			std::move(msgs), m_temperature, m_maxTokens,
			[this, myGen](const std::string& tok) {
				std::lock_guard<std::mutex> lk(m_mtx);
				if (myGen != m_generation) return; // superseded
				m_partial += tok;
			},
			[this, myGen](bool ok, const std::string& err) {
				std::lock_guard<std::mutex> lk(m_mtx);
				if (myGen != m_generation) return; // superseded
				m_streaming = false;
				m_activeId  = 0;
				if (ok) {
					m_history.push_back({"assistant", m_partial});
					m_partial.clear();
				} else {
					m_lastError = err;
					m_partial.clear();
				}
			});

		std::lock_guard<std::mutex> lk(m_mtx);
		if (myGen == m_generation) m_activeId = id;
	}

	// Abort any in-flight reply without touching history.
	void cancel() {
		uint64_t id;
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			id = m_activeId;
			m_activeId = 0;
			m_streaming = false;
			m_partial.clear();
			m_generation++;
		}
		if (id) m_client.cancel(id);
	}

	Snapshot snapshot() const {
		std::lock_guard<std::mutex> lk(m_mtx);
		return { m_history, m_partial, m_streaming, m_lastError };
	}

	bool streaming() const {
		std::lock_guard<std::mutex> lk(m_mtx);
		return m_streaming;
	}

private:
	LlmClient&               m_client;
	float                    m_temperature;
	int                      m_maxTokens;

	mutable std::mutex       m_mtx;
	std::vector<ChatMessage> m_history;
	std::string              m_partial;
	std::string              m_lastError;
	bool                     m_streaming = false;
	uint64_t                 m_activeId  = 0;
	uint64_t                 m_generation = 0;
};

} // namespace civcraft::llm

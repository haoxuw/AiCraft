#pragma once

// DialogPanel — chat window for talking to a humanoid NPC.
//
// Opens on T-key over a humanoid with `dialog_system_prompt` set on its
// artifact. Owns the per-conversation LlmSession and input buffer. Rule 5
// compliant: nothing routes through the server — tokens stream from the
// locally-run llama-server sidecar straight to this client's screen.
//
// Text input is fed via GLFW char+key callbacks pumped through Game (no
// ImGui). Live streaming tokens are rendered in a dim color until the turn
// completes, then committed to history.

#include "client/audio.h"
#include "client/audio_capture.h"
#include "client/rhi/rhi.h"
#include "client/ui_kit.h"
#include "logic/artifact_registry.h"
#include "llm/llm_client.h"
#include "llm/llm_session.h"
#include "llm/tts_client.h"
#include "llm/whisper_client.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace civcraft::vk {

class DialogPanel {
public:
	using EntityId = uint64_t;

	bool isOpen() const { return m_open; }
	EntityId target() const { return m_target; }

	// Returns false if the artifact entry has no dialog persona — caller
	// should treat that as "this NPC doesn't talk".
	// `audio`+`whisper` are optional (push-to-talk); `tts`+`audioOut` are
	// optional (voice playback of NPC replies).
	bool open(EntityId target, const std::string& npcName,
	          const civcraft::ArtifactEntry& artifact,
	          civcraft::llm::LlmClient& client,
	          civcraft::AudioCapture* audio = nullptr,
	          civcraft::llm::WhisperClient* whisper = nullptr,
	          civcraft::llm::TtsClient* tts = nullptr,
	          civcraft::AudioManager* audioOut = nullptr) {
		auto sysIt = artifact.fields.find("dialog_system_prompt");
		if (sysIt == artifact.fields.end() || sysIt->second.empty()) return false;

		float temp = 0.7f;
		auto tIt = artifact.fields.find("dialog_temperature");
		if (tIt != artifact.fields.end() && !tIt->second.empty()) {
			float v = std::atof(tIt->second.c_str());
			if (v > 0.0f && v < 2.0f) temp = v;
		}
		std::string greeting;
		auto gIt = artifact.fields.find("dialog_greeting");
		if (gIt != artifact.fields.end()) greeting = gIt->second;

		m_session  = std::make_unique<civcraft::llm::LlmSession>(client, sysIt->second, temp);
		m_target   = target;
		m_npcName  = npcName;
		m_greeting = std::move(greeting);
		m_input.clear();
		m_display.clear();
		if (!m_greeting.empty()) m_display.push_back({Role::Npc, m_greeting});
		m_audio    = audio;
		m_whisper  = whisper;
		m_tts      = tts;
		m_audioOut = audioOut;
		m_sttRecording = false;
		m_sttStatus.clear();
		m_ttsTurnCount = 0;
		m_ttsSpokenChars = 0;
		{
			std::lock_guard<std::mutex> lk(m_sttMtx);
			m_sttResult.clear();
			m_sttPending = false;
		}
		m_open = true;
		return true;
	}

	void close() {
		if (m_audio && m_sttRecording) m_audio->stopStream();
		m_sttRecording = false;
		m_open = false;
		m_target = 0;
		m_session.reset();
		m_input.clear();
		m_display.clear();
		m_audio = nullptr;
		m_whisper = nullptr;
		m_tts = nullptr;
		m_audioOut = nullptr;
		m_sttStatus.clear();
		m_ttsTurnCount = 0;
		m_ttsSpokenChars = 0;
		{
			std::lock_guard<std::mutex> lk(m_sttMtx);
			m_sttResult.clear();
			m_sttPending = false;
		}
	}

	// Push-to-talk: caller polls the physical Y key each frame and passes
	// the held state in. On press-edge we start mic capture; on release-edge
	// we stop it, drain to WAV, and send it to whisper. The transcript is
	// appended to m_input when it arrives (from the whisper worker thread).
	// No-op if this panel wasn't opened with audio+whisper.
	void onPushToTalk(bool yHeld) {
		if (!m_open || !m_audio || !m_whisper || !m_audio->isReady()) return;
		if (m_session && m_session->streaming()) return;

		if (yHeld && !m_sttRecording) {
			if (m_audio->startStream()) {
				m_sttRecording = true;
				m_sttStatus = "Listening… (release Y)";
			}
		} else if (!yHeld && m_sttRecording) {
			m_audio->stopStream();
			m_sttRecording = false;
			auto wav = m_audio->drainAsWav();
			// 44-byte header + ~0.25 s audio @ 16 kHz mono s16 = 8 KB min.
			// Anything shorter is almost certainly a misclick.
			if (wav.size() > 44 + 8000) {
				{
					std::lock_guard<std::mutex> lk(m_sttMtx);
					m_sttPending = true;
				}
				m_sttStatus = "Transcribing…";
				m_whisper->transcribe(std::move(wav),
					[this](bool ok, std::string text) {
						std::lock_guard<std::mutex> lk(m_sttMtx);
						m_sttPending = false;
						if (ok && !text.empty()) {
							m_sttResult = std::move(text);
						} else {
							m_sttStatus = ok ? "(no speech heard)" : "(stt failed)";
						}
					});
			} else {
				m_sttStatus.clear();
			}
		}

		// Pump any completed transcript into the input buffer. Done on the
		// main thread so we don't touch m_input from the whisper worker.
		std::string pending;
		{
			std::lock_guard<std::mutex> lk(m_sttMtx);
			if (!m_sttResult.empty()) {
				pending = std::move(m_sttResult);
				m_sttResult.clear();
			}
		}
		if (!pending.empty()) {
			if (!m_input.empty() && m_input.back() != ' ') m_input.push_back(' ');
			m_input += pending;
			if (m_input.size() > kMaxInputChars) m_input.resize(kMaxInputChars);
			m_sttStatus.clear();
		}
	}

	// GLFW char callback → printable codepoint. We only accept ASCII-range
	// (32-126) for the default font atlas; everything else is dropped. If the
	// game ever ships a richer font we lift this cap.
	void onChar(uint32_t codepoint) {
		if (!m_open) return;
		if (codepoint < 32 || codepoint > 126) return;
		if (m_input.size() >= kMaxInputChars) return;
		if (m_session && m_session->streaming()) return; // don't edit during stream
		// While push-to-talk is active, swallow 'y'/'Y' so holding Y doesn't
		// also type letters into the input buffer.
		if (m_audio && m_whisper && (codepoint == 'y' || codepoint == 'Y')) return;
		m_input.push_back((char)codepoint);
	}

	// GLFW key callback for non-printable keys. Returns true if the panel
	// consumed the key (caller should suppress its normal handling).
	bool onKey(int glfwKey) {
		if (!m_open) return false;
		// These constants mirror GLFW but we don't #include GLFW here to keep
		// this header light — caller passes the key code through.
		constexpr int KEY_BACKSPACE = 259;
		constexpr int KEY_ENTER     = 257;
		constexpr int KEY_ESCAPE    = 256;

		if (glfwKey == KEY_BACKSPACE) {
			if (m_session && m_session->streaming()) return true;
			if (!m_input.empty()) m_input.pop_back();
			return true;
		}
		if (glfwKey == KEY_ENTER) {
			submit();
			return true;
		}
		if (glfwKey == KEY_ESCAPE) {
			close();
			return true;
		}
		return false;
	}

	// Per-frame pump: peel finished sentences off the streaming reply and
	// hand them to piper. Called by Game while the panel is open.
	//
	// We track `m_ttsSpokenChars` — an index into the assistant's current
	// turn (committed history[-1] if streaming is over, else snap.partial).
	// Anything from that index up to the last sentence-ending punctuation
	// gets flushed to TTS in one chunk; the index advances past it so we
	// never speak the same fragment twice.
	//
	// `m_ttsTurnCount` is the assistant-turn counter; it bumps whenever a
	// new turn starts, resetting the spoken-char cursor to 0.
	void tickVoice() {
		if (!m_open || !m_tts || !m_audioOut) return;
		auto snap = m_session ? m_session->snapshot()
		                      : civcraft::llm::LlmSession::Snapshot{};

		// Count committed assistant turns. While streaming, the current turn
		// is NOT yet in history — it's in snap.partial.
		size_t assistantTurns = 0;
		for (auto& m : snap.history) if (m.role == "assistant") assistantTurns++;

		// Total turns including the in-flight one.
		size_t totalTurns = assistantTurns + (snap.streaming ? 1 : 0);
		if (totalTurns > m_ttsTurnCount) {
			// A new assistant turn began; reset cursor.
			m_ttsTurnCount   = totalTurns;
			m_ttsSpokenChars = 0;
		}

		// Pick the active text for the current turn.
		const std::string* active = nullptr;
		if (snap.streaming) {
			active = &snap.partial;
		} else if (!snap.history.empty() && snap.history.back().role == "assistant") {
			active = &snap.history.back().content;
		}
		if (!active) return;

		size_t pos = m_ttsSpokenChars;
		if (pos >= active->size()) {
			// Streaming finished & everything already spoken: nothing to do.
			return;
		}

		// Find the last sentence boundary in active->substr(pos). Speak up
		// to and including it. If streaming has ended, speak whatever is
		// left.
		const std::string& s = *active;
		size_t lastEnd = std::string::npos;
		for (size_t i = pos; i < s.size(); ++i) {
			char c = s[i];
			if (c == '.' || c == '!' || c == '?' || c == '\n') {
				// Require following whitespace or end-of-stream to avoid
				// chopping "e.g." mid-abbreviation.
				bool trailingOk = (i + 1 == s.size()) ||
				                   s[i + 1] == ' ' || s[i + 1] == '\n';
				if (!trailingOk && snap.streaming) continue;
				lastEnd = i + 1;  // include the punctuation
			}
		}

		size_t chunkEnd = 0;
		if (lastEnd != std::string::npos) {
			chunkEnd = lastEnd;
		} else if (!snap.streaming) {
			// Stream ended with no terminal punctuation — flush the rest.
			chunkEnd = s.size();
		} else {
			return;  // still streaming, no sentence boundary yet
		}

		if (chunkEnd <= pos) return;
		std::string chunk = s.substr(pos, chunkEnd - pos);
		m_ttsSpokenChars = chunkEnd;

		civcraft::AudioManager* out = m_audioOut;
		{
			std::lock_guard<std::mutex> lk(m_ttsMtx);
			m_ttsInFlight++;
		}
		m_tts->speak(std::move(chunk),
			[this, out](bool ok, std::string wavPath) {
				{
					std::lock_guard<std::mutex> lk(m_ttsMtx);
					if (m_ttsInFlight > 0) m_ttsInFlight--;
				}
				if (!ok || wavPath.empty()) return;
				if (out) out->playFile(wavPath, 1.0f);
			});
	}

	// True while any TTS chunk is between speak() and onDone — used by the
	// title bar to show a small "speaking…" cue. Approximate (doesn't track
	// the actual miniaudio playback end), but good enough as a subtitle-
	// sync hint: while this is true, the newest sentence in history is the
	// one being synthesized or about to play.
	bool ttsActive() const {
		std::lock_guard<std::mutex> lk(m_ttsMtx);
		return m_ttsInFlight > 0;
	}

	// Append streaming text into the display. Called each render so the UI
	// shows partial replies as they arrive.
	void render(rhi::IRhi* r, float aspect) {
		if (!m_open) return;
		using namespace ui::color;

		// Scrim behind the panel (tall enough the player can't see the crosshair).
		const float scrim[4] = {0.0f, 0.0f, 0.0f, 0.45f};
		r->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, scrim);

		// Panel geometry — anchored low-center so the player stays visible.
		const float panelW = 1.30f;
		const float panelH = 0.80f;
		const float panelX = -panelW * 0.5f;
		const float panelY = -0.90f;

		const float shadow[4] = {0.00f, 0.00f, 0.00f, 0.55f};
		const float fill[4]   = {0.08f, 0.06f, 0.05f, 0.96f};
		const float brass[4]  = {0.65f, 0.48f, 0.20f, 1.00f};
		ui::drawShadowPanel(r, panelX, panelY, panelW, panelH,
			shadow, fill, brass, 0.003f);

		// Title bar.
		const float titleH  = 0.070f;
		const float titleY  = panelY + panelH - titleH;
		const float titleBg[4] = {0.14f, 0.10f, 0.07f, 0.95f};
		r->drawRect2D(panelX + 0.008f, titleY, panelW - 0.016f, titleH - 0.008f, titleBg);
		const float titleCol[4] = {1.0f, 0.85f, 0.45f, 1.0f};
		std::string title = m_npcName.empty() ? std::string("Talk") : m_npcName;
		ui::drawCenteredTitle(r, title.c_str(),
			0.0f, titleY + 0.018f, 0.95f, titleCol);
		// Speaking indicator — small green dot in the title bar while TTS
		// is mid-flight. Approximate subtitle sync: while this is lit, the
		// newest sentence in history is being synthesized or is queued for
		// playback through AudioManager.
		bool speaking = false;
		{
			std::lock_guard<std::mutex> lk(m_ttsMtx);
			speaking = m_ttsInFlight > 0;
		}
		if (speaking) {
			const float green[4] = {0.45f, 0.90f, 0.50f, 1.0f};
			r->drawRect2D(panelX + 0.030f, titleY + 0.028f, 0.020f, 0.020f, green);
		}
		const float hintCol[4] = {0.70f, 0.65f, 0.55f, 0.90f};
		std::string hint = (m_audio && m_whisper && m_audio->isReady())
			? "[Hold Y] speak  [Enter] send  [Esc] close"
			: "[Enter] send  [Esc] close";
		// Hint width scales with char count; use panel right-align.
		float hintW = (float)hint.size() * ui::kCharWNdc * 0.55f;
		r->drawText2D(hint.c_str(),
			panelX + panelW - hintW - 0.020f, titleY + 0.024f, 0.55f, hintCol);

		// Input row at the bottom.
		const float inputH = 0.065f;
		const float inputY = panelY + 0.025f;
		const float inputX = panelX + 0.025f;
		const float inputW = panelW - 0.050f;
		const float inputBg[4] = {0.04f, 0.03f, 0.03f, 0.98f};
		r->drawRect2D(inputX, inputY, inputW, inputH, inputBg);
		ui::drawOutline(r, inputX, inputY, inputW, inputH, 0.0015f, brass);

		float caretBlink = 0;
		// A crude 0.5s blink driven by rendered-frame count so we don't need
		// the wall-clock threaded in.
		m_caretFrames++;
		caretBlink = ((m_caretFrames / 30) & 1) ? 0.0f : 1.0f;

		const float inputPadX = 0.016f;
		bool streaming = m_session && m_session->streaming();
		const float* inputCol = streaming ? kTextDim : kText;
		if (streaming && m_input.empty()) {
			r->drawText2D("…thinking…",
				inputX + inputPadX, inputY + 0.022f, 0.70f, kTextHint);
		} else if (!m_input.empty()) {
			r->drawText2D(m_input.c_str(),
				inputX + inputPadX, inputY + 0.022f, 0.70f, inputCol);
		} else if (!m_sttStatus.empty()) {
			const float stt[4] = {0.55f, 0.85f, 0.55f, 1.0f};
			r->drawText2D(m_sttStatus.c_str(),
				inputX + inputPadX, inputY + 0.022f, 0.70f, stt);
		} else {
			r->drawText2D("Say something…",
				inputX + inputPadX, inputY + 0.022f, 0.70f, kTextHint);
		}
		// Red recording dot on the right side while mic is live.
		if (m_sttRecording) {
			const float rec[4] = {0.95f, 0.20f, 0.20f, 1.0f};
			r->drawRect2D(inputX + inputW - 0.045f, inputY + 0.022f, 0.020f, 0.020f, rec);
		}
		// Caret after input text.
		if (!streaming && caretBlink > 0.5f) {
			float charW = ui::kCharWNdc * 0.70f;
			float caretX = inputX + inputPadX + (float)m_input.size() * charW + 0.002f;
			const float caretCol[4] = {1.0f, 0.95f, 0.55f, 0.95f};
			r->drawRect2D(caretX, inputY + 0.014f, 0.003f, 0.040f, caretCol);
		}

		// Scroll back history (above input, below title).
		const float historyTop    = titleY - 0.025f;
		const float historyBottom = inputY + inputH + 0.020f;
		renderHistory(r, panelX + 0.030f, historyBottom, panelW - 0.060f,
		              historyTop - historyBottom);
		(void)aspect;
	}

private:
	enum class Role : uint8_t { Player, Npc, Error };
	struct Line {
		Role role;
		std::string text;
	};

	void submit() {
		if (!m_session) return;
		if (m_session->streaming()) return;
		if (m_input.empty()) return;
		std::string text = m_input;
		m_input.clear();
		m_display.push_back({Role::Player, text});
		m_session->send(std::move(text));
	}

	// Render an NPC/player transcript; pull the latest LLM snapshot fresh
	// each frame so streaming tokens animate into view.
	void renderHistory(rhi::IRhi* r, float x, float y, float w, float h) {
		using namespace ui::color;

		// Compose drawable lines from committed history + live stream.
		auto snap = m_session ? m_session->snapshot() : civcraft::llm::LlmSession::Snapshot{};
		std::vector<Line> lines = m_display;

		// Walk committed LLM history, appending anything past what m_display
		// already accumulated. m_display tracks user turns the moment they're
		// sent; committed assistant turns land once streaming finishes, so
		// we mirror them into m_display on the next render.
		size_t playerTurnsInDisplay = 0, npcTurnsInDisplay = 0;
		for (auto& L : lines) {
			if (L.role == Role::Player) playerTurnsInDisplay++;
			else if (L.role == Role::Npc) npcTurnsInDisplay++;
		}
		// Greeting counts as an npc turn in display but not in history.
		size_t greetingOffset = m_greeting.empty() ? 0 : 1;

		size_t npcFromHistory = 0;
		for (auto& m : snap.history) {
			if (m.role == "assistant") npcFromHistory++;
		}
		// If history has more assistant turns than display (minus greeting),
		// append the tail ones to display permanently.
		if (npcFromHistory + greetingOffset > npcTurnsInDisplay) {
			// Walk history again, pushing the missing tail.
			size_t needed = npcFromHistory + greetingOffset - npcTurnsInDisplay;
			for (auto it = snap.history.rbegin();
			     it != snap.history.rend() && needed > 0; ++it) {
				if (it->role == "assistant") {
					auto insertPos = m_display.end();
					m_display.insert(insertPos, {Role::Npc, it->content});
					needed--;
				}
			}
			lines = m_display;
		}

		// Streaming partial appears at the end dimmed.
		if (snap.streaming && !snap.partial.empty()) {
			lines.push_back({Role::Npc, snap.partial});
		}
		if (!snap.lastError.empty()) {
			lines.push_back({Role::Error, std::string("[") + snap.lastError + "]"});
		}

		// Wrap each line to panel width, paint bottom-up until h is filled.
		const float scale   = 0.68f;
		const float charW   = ui::kCharWNdc * scale;
		const float lineH   = 0.040f;
		const float pad     = 0.010f;
		int charsPerLine    = (int)((w - pad * 2) / charW);
		if (charsPerLine < 10) charsPerLine = 10;

		// Build wrapped lines (role-tagged chunks) bottom-up.
		struct Row { Role role; std::string text; };
		std::vector<Row> rows;
		for (auto& L : lines) {
			std::string prefix = (L.role == Role::Player) ? "You: "
			                   : (L.role == Role::Error)  ? ""
			                                               : m_npcName + ": ";
			std::string full = prefix + L.text;
			for (size_t i = 0; i < full.size(); i += (size_t)charsPerLine) {
				rows.push_back({L.role,
					full.substr(i, (size_t)charsPerLine)});
			}
			rows.push_back({L.role, ""}); // paragraph gap
		}
		if (!rows.empty() && rows.back().text.empty()) rows.pop_back();

		// Paint bottom-up.
		float cy = y + pad;
		for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
			if (cy + lineH > y + h) break;
			if (!it->text.empty()) {
				const float* col = kText;
				if      (it->role == Role::Player) col = kText;
				else if (it->role == Role::Npc)    col = kTextDim;
				else                               col = kDanger;
				r->drawText2D(it->text.c_str(), x + pad, cy, scale, col);
			}
			cy += lineH;
		}
	}

	bool                                         m_open = false;
	EntityId                                     m_target = 0;
	std::string                                  m_npcName;
	std::string                                  m_greeting;
	std::string                                  m_input;
	std::vector<Line>                            m_display;
	std::unique_ptr<civcraft::llm::LlmSession>   m_session;
	int                                          m_caretFrames = 0;

	// Push-to-talk state. Borrowed pointers — owned by Game.
	civcraft::AudioCapture*                      m_audio   = nullptr;
	civcraft::llm::WhisperClient*                m_whisper = nullptr;
	bool                                         m_sttRecording = false;
	std::string                                  m_sttStatus;      // main-thread only
	mutable std::mutex                           m_sttMtx;         // guards m_sttResult + m_sttPending
	std::string                                  m_sttResult;
	bool                                         m_sttPending = false;

	// TTS sentence-chunk streaming state. Borrowed pointers — owned by Game.
	civcraft::llm::TtsClient*                    m_tts      = nullptr;
	civcraft::AudioManager*                      m_audioOut = nullptr;
	size_t                                       m_ttsTurnCount   = 0;  // assistant turns seen so far
	size_t                                       m_ttsSpokenChars = 0;  // index into current turn
	mutable std::mutex                           m_ttsMtx;
	int                                          m_ttsInFlight    = 0;  // speak() calls pending

	static constexpr size_t kMaxInputChars = 200;
};

} // namespace civcraft::vk

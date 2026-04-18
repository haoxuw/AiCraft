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

#include "client/rhi/rhi.h"
#include "client/ui_kit.h"
#include "logic/artifact_registry.h"
#include "llm/llm_client.h"
#include "llm/llm_session.h"

#include <memory>
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
	bool open(EntityId target, const std::string& npcName,
	          const civcraft::ArtifactEntry& artifact,
	          civcraft::llm::LlmClient& client) {
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
		m_open = true;
		return true;
	}

	void close() {
		m_open = false;
		m_target = 0;
		m_session.reset();
		m_input.clear();
		m_display.clear();
	}

	// GLFW char callback → printable codepoint. We only accept ASCII-range
	// (32-126) for the default font atlas; everything else is dropped. If the
	// game ever ships a richer font we lift this cap.
	void onChar(uint32_t codepoint) {
		if (!m_open) return;
		if (codepoint < 32 || codepoint > 126) return;
		if (m_input.size() >= kMaxInputChars) return;
		if (m_session && m_session->streaming()) return; // don't edit during stream
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
		const float hintCol[4] = {0.70f, 0.65f, 0.55f, 0.90f};
		r->drawText2D("[Enter] send  [Esc] close",
			panelX + panelW - 0.40f, titleY + 0.024f, 0.55f, hintCol);

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
		} else {
			r->drawText2D("Say something…",
				inputX + inputPadX, inputY + 0.022f, 0.70f, kTextHint);
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

	static constexpr size_t kMaxInputChars = 200;
};

} // namespace civcraft::vk

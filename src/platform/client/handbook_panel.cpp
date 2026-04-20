#include "client/handbook_panel.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"
#include "client/audio.h"
#include "llm/tts_client.h"
#include "llm/tts_voice_mux.h"
#include "logic/artifact_registry.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace civcraft::vk {

namespace {

using namespace ui::color;

// Rectangular button with hover + click. Returns true the frame the mouse
// releases over the rect (matches drawMenuButton's release-edge semantics).
bool drawButton(rhi::IRhi* r, float x, float y, float w, float h,
                const char* label, float scale,
                float mouseX, float mouseY, bool mouseReleased) {
	bool hover = ui::rectContainsNdc(x, y, w, h, mouseX, mouseY);

	const float bgIdle[4] = {0.10f, 0.09f, 0.09f, 0.92f};
	const float bgHov [4] = {0.22f, 0.16f, 0.10f, 0.96f};
	const float brass [4] = {0.72f, 0.54f, 0.22f, 1.00f};
	const float brassLt[4]= {0.95f, 0.78f, 0.35f, 1.00f};
	const float txtIdle[4]= {0.85f, 0.82f, 0.78f, 1.00f};
	const float txtHov[4] = {1.00f, 0.88f, 0.48f, 1.00f};

	r->drawRect2D(x, y, w, h, hover ? bgHov : bgIdle);
	ui::drawOutline(r, x, y, w, h, 0.0020f, hover ? brassLt : brass);

	ui::drawCenteredText(r, label, x + w * 0.5f,
		y + h * 0.5f - ui::kCharHNdc * scale * 0.5f,
		scale, hover ? txtHov : txtIdle);

	return hover && mouseReleased;
}

} // namespace

void HandbookPanel::render(Game& g) {
	rhi::IRhi*  R = g.m_rhi;
	GLFWwindow* W = g.m_window;

	const float mx     = g.m_mouseNdcX;
	const float my     = g.m_mouseNdcY;
	const bool  mClick = g.m_mouseLReleased;

	// ── Category/entry state ─────────────────────────────────────────────
	m_categoriesCache = g.m_artifactRegistry.allCategories();
	// "voice" is a synthetic category — not in ArtifactRegistry. It lists
	// piper .onnx voices found by TtsVoiceMux. Only offered when the mux
	// loaded at startup; if tts_setup was never run, this tab is hidden.
	if (g.m_ttsMux) m_categoriesCache.push_back("voice");
	if (m_categoriesCache.empty()) {
		g.m_shell.title = "Handbook";
		g.m_shell.previewId.clear();
		g.m_shell.drawChrome(R);
		ui::drawCenteredText(R, "No artifacts registered.",
			0.0f, 0.0f, 0.90f, kDanger);
		return;
	}
	m_categoryCursor = std::clamp(m_categoryCursor, 0,
		(int)m_categoriesCache.size() - 1);

	auto bumpCat = [&](int delta) {
		int n = (int)m_categoriesCache.size();
		m_categoryCursor = (m_categoryCursor + delta + n) % n;
		m_entryCursor = 0;
		m_entryScroll = 0;
	};

	if (ui::keyEdge(W, GLFW_KEY_LEFT)  || ui::keyEdge(W, GLFW_KEY_A)) bumpCat(-1);
	if (ui::keyEdge(W, GLFW_KEY_RIGHT) || ui::keyEdge(W, GLFW_KEY_D)) bumpCat(+1);

	const std::string& cat = m_categoriesCache[m_categoryCursor];
	const bool isVoice = (cat == "voice");
	auto entries = isVoice ? std::vector<const ArtifactEntry*>{}
	                       : g.m_artifactRegistry.byCategory(cat);
	std::vector<std::string> voiceNames;
	if (isVoice && g.m_ttsMux) voiceNames = g.m_ttsMux->voiceNames();

	const int entryCount = isVoice ? (int)voiceNames.size()
	                               : (int)entries.size();

	if (entryCount > 0) {
		bool entryNext = ui::keyEdge(W, GLFW_KEY_DOWN) || ui::keyEdge(W, GLFW_KEY_S);
		bool entryPrev = ui::keyEdge(W, GLFW_KEY_UP)   || ui::keyEdge(W, GLFW_KEY_W);
		if (entryNext) m_entryCursor = (m_entryCursor + 1) % entryCount;
		if (entryPrev) m_entryCursor = (m_entryCursor - 1 + entryCount) % entryCount;
		m_entryCursor = std::clamp(m_entryCursor, 0, entryCount - 1);
	} else {
		m_entryCursor = 0;
	}

	const ArtifactEntry* selected =
		(isVoice || entries.empty() || m_entryCursor >= (int)entries.size())
		? nullptr : entries[m_entryCursor];

	// ── Shell state ──────────────────────────────────────────────────────
	m_titleBuf = "Handbook: " + cat;
	g.m_shell.title = m_titleBuf.c_str();

	bool hasModel = (cat == "living" || cat == "item" ||
	                 cat == "annotation" || cat == "model");
	if (selected && hasModel) g.m_shell.previewId = selected->id;
	else                       g.m_shell.previewId.clear();

	g.m_shell.drawChrome(R);

	auto L = g.m_shell.leftBar();
	auto B = g.m_shell.bottomBar();
	auto P = g.m_shell.previewArea();

	// ── LeftBar: category strip with clickable arrows ───────────────────
	// Banner sits below the title strip (titleH 0.080 + 0.016 top pad +
	// gap = 0.112). Arrow buttons flank a centered category label.
	const float catY  = L.y + L.h - 0.112f - 0.054f;
	const float catCx = L.x + L.w * 0.5f;

	const float arrowW = 0.040f;
	const float arrowH = 0.046f;
	const float arrowYBtn = catY - 0.008f;

	if (drawButton(R, L.x + 0.024f, arrowYBtn, arrowW, arrowH,
	               "<", 0.95f, mx, my, mClick)) {
		bumpCat(-1);
		entries = g.m_artifactRegistry.byCategory(m_categoriesCache[m_categoryCursor]);
		selected = entries.empty() ? nullptr : entries[0];
	}
	if (drawButton(R, L.x + L.w - 0.024f - arrowW, arrowYBtn,
	               arrowW, arrowH, ">", 0.95f, mx, my, mClick)) {
		bumpCat(+1);
		entries = g.m_artifactRegistry.byCategory(m_categoriesCache[m_categoryCursor]);
		selected = entries.empty() ? nullptr : entries[0];
	}

	const std::string& curCat = m_categoriesCache[m_categoryCursor];
	ui::drawCenteredText(R, curCat.c_str(), catCx, catY, 0.85f, kText);

	char countBuf[64];
	std::snprintf(countBuf, sizeof(countBuf), "%d / %d",
		entryCount == 0 ? 0 : m_entryCursor + 1, entryCount);
	ui::drawCenteredText(R, countBuf, catCx, catY - 0.034f, 0.60f, kTextHint);

	// ── LeftBar: clickable entry list ───────────────────────────────────
	const float listTop  = catY - 0.072f;
	const float listBot  = L.y + 0.040f;
	const float rowH     = 0.036f;
	int maxRows = std::max(1, (int)((listTop - listBot) / rowH));

	if (m_entryCursor < m_entryScroll) m_entryScroll = m_entryCursor;
	if (m_entryCursor >= m_entryScroll + maxRows)
		m_entryScroll = m_entryCursor - maxRows + 1;
	m_entryScroll = std::max(0, m_entryScroll);

	if (entryCount == 0) {
		ui::drawCenteredText(R, "(empty)", catCx,
			listTop - 0.040f, 0.75f, kTextHint);
	} else {
		float y = listTop;
		int end = std::min(entryCount, m_entryScroll + maxRows);
		const float rowX = L.x + 0.030f;
		const float rowW = L.w - 0.060f;
		for (int i = m_entryScroll; i < end; ++i) {
			float ry = y - 0.006f;
			float rh = rowH - 0.004f;
			bool hover = ui::rectContainsNdc(rowX, ry, rowW, rh, mx, my);
			bool sel   = (i == m_entryCursor);

			// Mouse hover snaps the keyboard cursor here too — matches
			// Civilopedia muscle memory: whatever is under the mouse is the
			// "current" entry.
			if (hover) m_entryCursor = i;
			if (hover && mClick && !isVoice) selected = entries[i];

			if (sel) {
				const float selBg[4] = {0.28f, 0.20f, 0.10f, 0.78f};
				R->drawRect2D(rowX, ry, rowW, rh, selBg);
				const float brass[4] = {0.78f, 0.58f, 0.22f, 1.0f};
				R->drawRect2D(rowX, ry, 0.006f, rh, brass);
			} else if (hover) {
				const float hovBg[4] = {0.18f, 0.14f, 0.10f, 0.55f};
				R->drawRect2D(rowX, ry, rowW, rh, hovBg);
			}

			const char* label = isVoice
				? voiceNames[i].c_str()
				: (entries[i]->name.empty() ? entries[i]->id.c_str()
				                            : entries[i]->name.c_str());
			R->drawText2D(label, rowX + 0.016f, y, 0.70f,
				sel ? kText : (hover ? kText : kTextDim));
			y -= rowH;
		}

		if (entryCount > maxRows) {
			char sb[32];
			std::snprintf(sb, sizeof(sb), "%d-%d of %d",
				m_entryScroll + 1, end, entryCount);
			ui::drawCenteredText(R, sb, catCx, listBot - 0.010f,
				0.55f, kTextHint);
		}
	}

	// Re-resolve selected after possible mouse-driven cursor moves.
	selected = (isVoice || entries.empty() || m_entryCursor >= (int)entries.size())
		? nullptr : entries[m_entryCursor];
	if (selected && hasModel) g.m_shell.previewId = selected->id;

	// ── BottomBar: BACK button + hints ──────────────────────────────────
	const float btnW = 0.18f;
	const float btnH = B.h - 0.040f;
	const float btnX = B.x + 0.020f;
	const float btnY = B.y + (B.h - btnH) * 0.5f;
	if (drawButton(R, btnX, btnY, btnW, btnH, "BACK", 0.85f, mx, my, mClick)) {
		// Esc and BACK do the same thing: tear down preview/cover state and
		// hop back to whatever container hosted us. Menu → Main; in-game →
		// clear the overlay flag so the world resumes.
		if (g.m_state == GameState::Menu) g.m_menuScreen = MenuScreen::Main;
		else                              g.m_handbookOpen = false;
		g.m_shell.coverVisible = false;
		g.m_shell.previewId.clear();
		g.m_shell.title = "";
		return;
	}

	const float hintY = B.y + B.h * 0.5f - ui::kCharHNdc * 0.65f * 0.5f;
	const char* hint =
		"[Click] Select   [Left/Right] Category   [Up/Down] Entry   [Esc] Back";
	float hintW = ui::textWidthNdc(std::strlen(hint), 0.65f);
	R->drawText2D(hint, B.x + B.w - hintW - 0.028f, hintY, 0.65f, kTextDim);

	// ── Preview area: voice detail card ─────────────────────────────────
	// The voice tab has no ArtifactEntry to describe; instead we surface
	// the resolved .onnx path, what this voice would sound like, and a
	// Play Sample button that pipes through the same TTS stack DialogPanel
	// uses. Single-shot — no sequential queue here: a debounce prevents
	// rapid clicks from stacking overlapping piper jobs.
	if (isVoice && m_entryCursor >= 0 && m_entryCursor < (int)voiceNames.size()) {
		const std::string& voice = voiceNames[m_entryCursor];

		const float cardW = 0.60f;
		const float cardH = 0.34f;
		const float cardX = P.x + 0.040f;
		const float cardY = P.y + 0.030f;
		const float cardFill[4]   = {0.07f, 0.06f, 0.06f, 0.72f};
		const float cardShadow[4] = {0.00f, 0.00f, 0.00f, 0.40f};
		const float brass[4]      = {0.72f, 0.54f, 0.22f, 0.90f};
		ui::drawShadowPanel(R, cardX, cardY, cardW, cardH,
			cardShadow, cardFill, brass, 0.003f);

		R->drawText2D(voice.c_str(), cardX + 0.022f,
			cardY + cardH - 0.052f, 1.00f, kText);
		R->drawText2D("voice : piper .onnx", cardX + 0.022f,
			cardY + cardH - 0.090f, 0.60f, kTextHint);

		std::string sample = "Hello, traveller. This is the voice of " + voice + ".";
		std::string line = "Sample: " + sample;
		size_t maxChars = (size_t)((cardW - 0.044f) /
			(ui::kCharWNdc * 0.65f));
		if (line.size() > maxChars) line = line.substr(0, maxChars);
		R->drawText2D(line.c_str(), cardX + 0.022f,
			cardY + cardH - 0.132f, 0.65f, kTextDim);

		// NPCs that already route to this voice — tiny affordance so a
		// modder writing `dialog_voice: "..."` can see who picks it.
		std::string users;
		for (const ArtifactEntry* e : g.m_artifactRegistry.byCategory("living")) {
			auto it = e->fields.find("dialog_voice");
			if (it == e->fields.end()) continue;
			if (voice.find(it->second) != std::string::npos) {
				if (!users.empty()) users += ", ";
				users += e->name.empty() ? e->id : e->name;
			}
		}
		if (!users.empty()) {
			std::string u = "used by: " + users;
			if (u.size() > maxChars) u = u.substr(0, maxChars);
			R->drawText2D(u.c_str(), cardX + 0.022f,
				cardY + cardH - 0.170f, 0.60f, kTextDim);
		}

		const float btnW = 0.22f;
		const float btnH = 0.052f;
		const float btnX = cardX + 0.022f;
		const float btnY = cardY + 0.028f;
		auto now = std::chrono::steady_clock::now();
		bool cooldownActive =
			(now - m_lastVoiceSampleTime) < std::chrono::seconds(2);
		const char* btnLabel = cooldownActive ? "Synthesizing..." : "Play Sample";
		bool clicked = drawButton(R, btnX, btnY, btnW, btnH,
			btnLabel, 0.80f, mx, my, mClick);
		if (clicked && !cooldownActive && g.m_ttsMux) {
			if (auto* tts = g.m_ttsMux->clientFor(voice)) {
				AudioManager* audio = &g.m_audio;
				tts->speak(sample, [audio](bool ok, std::string path) {
					if (ok && !path.empty()) audio->playFile(path, 1.0f);
				});
				m_lastVoiceSampleTime = now;
			}
		}
	}

	// ── Preview area: info card ─────────────────────────────────────────
	if (selected) {
		const float cardW = 0.60f;
		const float cardH = 0.34f;
		const float cardX = P.x + 0.040f;
		const float cardY = P.y + 0.030f;
		const float cardFill[4]   = {0.07f, 0.06f, 0.06f, 0.72f};
		const float cardShadow[4] = {0.00f, 0.00f, 0.00f, 0.40f};
		const float brass[4]      = {0.72f, 0.54f, 0.22f, 0.90f};
		ui::drawShadowPanel(R, cardX, cardY, cardW, cardH,
			cardShadow, cardFill, brass, 0.003f);

		const char* nm = selected->name.empty()
			? selected->id.c_str() : selected->name.c_str();
		R->drawText2D(nm, cardX + 0.022f,
			cardY + cardH - 0.052f, 1.00f, kText);

		char idBuf[160];
		std::snprintf(idBuf, sizeof(idBuf), "%s : %s",
			selected->category.c_str(), selected->id.c_str());
		R->drawText2D(idBuf, cardX + 0.022f,
			cardY + cardH - 0.090f, 0.60f, kTextHint);

		float fieldY = cardY + cardH - 0.132f;
		if (!selected->description.empty()) {
			size_t maxChars = (size_t)((cardW - 0.044f) /
				(ui::kCharWNdc * 0.65f));
			std::string desc = selected->description;
			if (desc.size() > maxChars) desc = desc.substr(0, maxChars);
			R->drawText2D(desc.c_str(), cardX + 0.022f,
				fieldY, 0.65f, kTextDim);
			fieldY -= 0.038f;
		}

		if (!selected->subcategory.empty()) {
			char sb[96];
			std::snprintf(sb, sizeof(sb), "subcategory: %s",
				selected->subcategory.c_str());
			R->drawText2D(sb, cardX + 0.022f, fieldY, 0.60f, kTextDim);
			fieldY -= 0.034f;
		}

		if (!selected->tags.empty()) {
			std::string t = "tags: ";
			for (size_t i = 0; i < selected->tags.size(); ++i) {
				if (i) t += ", ";
				t += selected->tags[i];
			}
			size_t maxChars = (size_t)((cardW - 0.044f) /
				(ui::kCharWNdc * 0.60f));
			if (t.size() > maxChars) t = t.substr(0, maxChars);
			R->drawText2D(t.c_str(), cardX + 0.022f,
				fieldY, 0.60f, kTextDim);
		}
	}
}

} // namespace civcraft::vk

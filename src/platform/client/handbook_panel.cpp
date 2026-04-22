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
#include <string>
#include <vector>

namespace civcraft::vk {

namespace {

using namespace ui::color;

// Meta-grouping: flat tabs (living/item/block/behavior/model/…) roll up into
// a handful of player-facing sections. Each group's "All" sub-filter shows
// the union of its member categories; picking a specific sub-filter narrows
// to one underlying registry category. "voice" is synthetic — not a registry
// category — so it's a pseudo-sub only under Modding.
struct SubCat {
	const char* label;
	const char* registryCat;   // "" for synthetic (voice)
};
struct Group {
	const char* label;
	std::vector<SubCat> subs;  // [0] is always "All"
};

static const std::vector<Group>& groups() {
	static const std::vector<Group> g = {
		{ "Creatures", {
			{ "All", "" },
			{ "Living", "living" },
		}},
		{ "Items",    {
			{ "All", "" },
			{ "Items",     "item" },
			{ "Effects",   "effect" },
			{ "Resources", "resource" },
		}},
		{ "World",    {
			{ "All", "" },
			{ "Blocks",      "block" },
			{ "Structures",  "structure" },
			{ "Annotations", "annotation" },
			{ "Worlds",      "world" },
		}},
		{ "Gameplay", {
			{ "All", "" },
			{ "Behaviors", "behavior" },
		}},
		{ "Modding",  {
			{ "All", "" },
			{ "Models", "model" },
			{ "Voices", "" },   // synthetic — fed by TtsVoiceMux
		}},
	};
	return g;
}

static bool isVoiceSub(const Group& grp, int sub) {
	if (sub <= 0 || sub >= (int)grp.subs.size()) return false;
	return std::strcmp(grp.subs[sub].label, "Voices") == 0;
}

// True iff this group's "All" should include voices (only Modding).
static bool groupIncludesVoices(const Group& grp) {
	return std::strcmp(grp.label, "Modding") == 0;
}

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

// Pill-style sub-filter button; active when current, hover when mouse over.
bool drawPill(rhi::IRhi* r, float x, float y, float w, float h,
              const char* label, bool active,
              float mouseX, float mouseY, bool mouseReleased) {
	bool hover = ui::rectContainsNdc(x, y, w, h, mouseX, mouseY);

	const float bgIdle[4]   = {0.08f, 0.07f, 0.06f, 0.82f};
	const float bgHov[4]    = {0.18f, 0.14f, 0.09f, 0.90f};
	const float bgActive[4] = {0.34f, 0.24f, 0.10f, 0.92f};
	const float brass[4]    = {0.72f, 0.54f, 0.22f, 1.00f};
	const float brassLt[4]  = {0.95f, 0.78f, 0.35f, 1.00f};
	const float txtDim[4]   = {0.72f, 0.70f, 0.66f, 1.00f};
	const float txtActive[4]= {1.00f, 0.90f, 0.50f, 1.00f};

	const float* bg = active ? bgActive : (hover ? bgHov : bgIdle);
	r->drawRect2D(x, y, w, h, bg);
	ui::drawOutline(r, x, y, w, h, 0.0014f,
		(active || hover) ? brassLt : brass);

	const float scale = 0.58f;
	ui::drawCenteredText(r, label, x + w * 0.5f,
		y + h * 0.5f - ui::kCharHNdc * scale * 0.5f,
		scale, active ? txtActive : txtDim);

	return hover && mouseReleased;
}

} // namespace

void HandbookPanel::render(Game& g) {
	rhi::IRhi*  R = g.m_rhi;
	GLFWwindow* W = g.m_window;

	const float mx     = g.m_mouseNdcX;
	const float my     = g.m_mouseNdcY;
	const bool  mClick = g.m_mouseLReleased;

	const auto& GR = groups();
	m_groupCursor = std::clamp(m_groupCursor, 0, (int)GR.size() - 1);
	const Group* grp = &GR[m_groupCursor];
	m_subCursor = std::clamp(m_subCursor, 0, (int)grp->subs.size() - 1);

	auto bumpGroup = [&](int delta) {
		int n = (int)GR.size();
		m_groupCursor = (m_groupCursor + delta + n) % n;
		m_subCursor   = 0;
		m_entryCursor = 0;
		m_entryScroll = 0;
	};

	if (ui::keyEdge(W, GLFW_KEY_LEFT)  || ui::keyEdge(W, GLFW_KEY_A)) bumpGroup(-1);
	if (ui::keyEdge(W, GLFW_KEY_RIGHT) || ui::keyEdge(W, GLFW_KEY_D)) bumpGroup(+1);

	grp = &GR[m_groupCursor];

	// ── Build entry list for the current (group, sub) ───────────────────
	// Each item is either a real ArtifactEntry (living/item/block/…) or a
	// synthetic voice name. We keep them in two parallel vectors so the
	// selection logic stays simple and voice stays a first-class citizen
	// without needing a fake ArtifactEntry.
	std::vector<const ArtifactEntry*> entries;
	std::vector<std::string>          voices;
	std::vector<bool>                 rowIsVoice;   // parallel to display order

	auto appendCategory = [&](const std::string& cat) {
		auto v = g.m_artifactRegistry.byCategory(cat);
		for (auto* e : v) {
			entries.push_back(e);
			rowIsVoice.push_back(false);
		}
	};
	auto appendVoices = [&]() {
		if (!g.m_ttsMux) return;
		for (const auto& v : g.m_ttsMux->voiceNames()) {
			voices.push_back(v);
			rowIsVoice.push_back(true);
		}
	};

	if (m_subCursor == 0) {
		// "All" — union of every sub in this group (skipping the All pseudo).
		for (size_t i = 1; i < grp->subs.size(); ++i) {
			const SubCat& s = grp->subs[i];
			if (s.registryCat[0] != '\0') appendCategory(s.registryCat);
		}
		if (groupIncludesVoices(*grp)) appendVoices();
	} else {
		const SubCat& s = grp->subs[m_subCursor];
		if (isVoiceSub(*grp, m_subCursor)) appendVoices();
		else if (s.registryCat[0] != '\0') appendCategory(s.registryCat);
	}

	const int entryCount = (int)rowIsVoice.size();

	// ── Shell state ──────────────────────────────────────────────────────
	m_titleBuf = std::string("Handbook: ") + grp->label;
	g.m_shell.title = m_titleBuf.c_str();

	// Entry -> voice/entry index lookup table (row i in display order)
	auto entryIndexAt = [&](int row) -> int {
		int ei = 0;
		for (int i = 0; i <= row && i < entryCount; ++i) {
			if (!rowIsVoice[i] && i == row) return ei;
			if (!rowIsVoice[i]) ei++;
		}
		return -1;
	};
	auto voiceIndexAt = [&](int row) -> int {
		int vi = 0;
		for (int i = 0; i <= row && i < entryCount; ++i) {
			if (rowIsVoice[i] && i == row) return vi;
			if (rowIsVoice[i]) vi++;
		}
		return -1;
	};

	if (entryCount > 0) {
		bool entryNext = ui::keyEdge(W, GLFW_KEY_DOWN) || ui::keyEdge(W, GLFW_KEY_S);
		bool entryPrev = ui::keyEdge(W, GLFW_KEY_UP)   || ui::keyEdge(W, GLFW_KEY_W);
		if (entryNext) m_entryCursor = (m_entryCursor + 1) % entryCount;
		if (entryPrev) m_entryCursor = (m_entryCursor - 1 + entryCount) % entryCount;
		m_entryCursor = std::clamp(m_entryCursor, 0, entryCount - 1);
	} else {
		m_entryCursor = 0;
	}

	const bool curIsVoice = (entryCount > 0) && rowIsVoice[m_entryCursor];
	const ArtifactEntry* selected = nullptr;
	std::string selectedVoice;
	if (entryCount > 0) {
		if (curIsVoice) {
			int vi = voiceIndexAt(m_entryCursor);
			if (vi >= 0 && vi < (int)voices.size()) selectedVoice = voices[vi];
		} else {
			int ei = entryIndexAt(m_entryCursor);
			if (ei >= 0 && ei < (int)entries.size()) selected = entries[ei];
		}
	}

	// 3D preview — honor the selected entry's *own* category so a Creature
	// under a union view still pins the camera (its category is "living"),
	// while a Behavior under Gameplay does not (no model).
	auto categoryHasModel = [](const std::string& c) {
		return c == "living" || c == "item" || c == "annotation" || c == "model";
	};
	if (selected && categoryHasModel(selected->category))
		g.m_shell.previewId = selected->id;
	else
		g.m_shell.previewId.clear();

	g.m_shell.drawChrome(R);

	auto L = g.m_shell.leftBar();
	auto B = g.m_shell.bottomBar();
	auto P = g.m_shell.previewArea();

	// ── LeftBar: top-level group strip ──────────────────────────────────
	// Banner sits below the title strip (titleH 0.080 + 0.016 top pad).
	const float groupY = L.y + L.h - 0.112f - 0.054f;
	const float groupCx = L.x + L.w * 0.5f;

	const float arrowW = 0.040f;
	const float arrowH = 0.046f;
	const float arrowY = groupY - 0.008f;

	if (drawButton(R, L.x + 0.024f, arrowY, arrowW, arrowH,
	               "<", 0.95f, mx, my, mClick)) {
		bumpGroup(-1);
	}
	if (drawButton(R, L.x + L.w - 0.024f - arrowW, arrowY,
	               arrowW, arrowH, ">", 0.95f, mx, my, mClick)) {
		bumpGroup(+1);
	}

	grp = &GR[m_groupCursor];
	m_subCursor = std::clamp(m_subCursor, 0, (int)grp->subs.size() - 1);

	ui::drawCenteredText(R, grp->label, groupCx, groupY, 0.90f, kText);

	char countBuf[64];
	std::snprintf(countBuf, sizeof(countBuf), "%d / %d",
		entryCount == 0 ? 0 : m_entryCursor + 1, entryCount);
	ui::drawCenteredText(R, countBuf, groupCx, groupY - 0.034f, 0.58f, kTextHint);

	// ── LeftBar: sub-filter pills ───────────────────────────────────────
	// Two-per-row grid so any group (≤4 subs right now) fits cleanly. Pills
	// are only drawn when >1 sub; single-sub groups (Creatures, Gameplay)
	// skip the strip entirely and claim the vertical space for entries.
	float pillsBottom = groupY - 0.072f;
	const bool hasSubs = (grp->subs.size() > 1);
	if (hasSubs) {
		const float pillAreaTop  = groupY - 0.072f;
		const float pillH        = 0.034f;
		const float pillGap      = 0.006f;
		const float pillInsetX   = 0.024f;
		const float pillRowW     = L.w - 2.0f * pillInsetX;
		const int   pillsPerRow  = 2;
		const float pillW        = (pillRowW - pillGap * (pillsPerRow - 1))
		                           / pillsPerRow;

		for (int i = 0; i < (int)grp->subs.size(); ++i) {
			int row = i / pillsPerRow;
			int col = i % pillsPerRow;
			float px = L.x + pillInsetX + col * (pillW + pillGap);
			float py = pillAreaTop - (row + 1) * pillH - row * pillGap;
			bool active = (i == m_subCursor);
			if (drawPill(R, px, py, pillW, pillH,
			             grp->subs[i].label, active, mx, my, mClick)) {
				m_subCursor   = i;
				m_entryCursor = 0;
				m_entryScroll = 0;
			}
			pillsBottom = py;
		}
		pillsBottom -= 0.012f;
	}

	// ── LeftBar: clickable entry list ───────────────────────────────────
	const float listTop  = pillsBottom;
	const float listBot  = L.y + 0.040f;
	const float rowH     = 0.036f;
	int maxRows = std::max(1, (int)((listTop - listBot) / rowH));

	if (m_entryCursor < m_entryScroll) m_entryScroll = m_entryCursor;
	if (m_entryCursor >= m_entryScroll + maxRows)
		m_entryScroll = m_entryCursor - maxRows + 1;
	m_entryScroll = std::max(0, m_entryScroll);

	if (entryCount == 0) {
		ui::drawCenteredText(R, "(empty)", groupCx,
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

			// Mouse hover snaps the keyboard cursor here too.
			if (hover) m_entryCursor = i;
			if (hover && mClick) {
				m_entryCursor = i;
			}

			if (sel) {
				const float selBg[4] = {0.28f, 0.20f, 0.10f, 0.78f};
				R->drawRect2D(rowX, ry, rowW, rh, selBg);
				const float brass[4] = {0.78f, 0.58f, 0.22f, 1.0f};
				R->drawRect2D(rowX, ry, 0.006f, rh, brass);
			} else if (hover) {
				const float hovBg[4] = {0.18f, 0.14f, 0.10f, 0.55f};
				R->drawRect2D(rowX, ry, rowW, rh, hovBg);
			}

			std::string label;
			const float* txt = sel ? kText : (hover ? kText : kTextDim);
			if (rowIsVoice[i]) {
				int vi = voiceIndexAt(i);
				label = (vi >= 0 && vi < (int)voices.size()) ? voices[vi] : "";
			} else {
				int ei = entryIndexAt(i);
				if (ei >= 0 && ei < (int)entries.size()) {
					const ArtifactEntry* e = entries[ei];
					label = e->name.empty() ? e->id : e->name;
				}
			}
			ui::writeText(R, label.c_str(), rowX + 0.016f, y, 0.70f, txt);

			// In "All" views, a tiny right-aligned category tag disambiguates
			// entries drawn from different underlying categories.
			if (m_subCursor == 0 && !rowIsVoice[i]) {
				int ei = entryIndexAt(i);
				if (ei >= 0 && ei < (int)entries.size()) {
					const std::string& c = entries[ei]->category;
					float tw = ui::textWidthNdc(c.size(), 0.48f);
					ui::writeText(R, c.c_str(),
						rowX + rowW - tw - 0.010f,
						y + 0.002f, 0.48f, kTextHint);
				}
			} else if (m_subCursor == 0 && rowIsVoice[i]) {
				const char* c = "voice";
				float tw = ui::textWidthNdc(std::strlen(c), 0.48f);
				ui::writeText(R, c, rowX + rowW - tw - 0.010f,
					y + 0.002f, 0.48f, kTextHint);
			}

			y -= rowH;
		}

		if (entryCount > maxRows) {
			char sb[32];
			std::snprintf(sb, sizeof(sb), "%d-%d of %d",
				m_entryScroll + 1, end, entryCount);
			ui::drawCenteredText(R, sb, groupCx, listBot - 0.010f,
				0.55f, kTextHint);
		}
	}

	// Re-resolve selection after possible mouse-hover cursor moves.
	selected       = nullptr;
	selectedVoice.clear();
	if (entryCount > 0) {
		if (rowIsVoice[m_entryCursor]) {
			int vi = voiceIndexAt(m_entryCursor);
			if (vi >= 0 && vi < (int)voices.size()) selectedVoice = voices[vi];
		} else {
			int ei = entryIndexAt(m_entryCursor);
			if (ei >= 0 && ei < (int)entries.size()) selected = entries[ei];
		}
	}
	if (selected && categoryHasModel(selected->category))
		g.m_shell.previewId = selected->id;
	else
		g.m_shell.previewId.clear();

	// ── BottomBar: BACK button + hints ──────────────────────────────────
	const float btnW = 0.18f;
	const float btnH = B.h - 0.040f;
	const float btnX = B.x + 0.020f;
	const float btnY = B.y + (B.h - btnH) * 0.5f;
	if (drawButton(R, btnX, btnY, btnW, btnH, "BACK", 0.85f, mx, my, mClick)) {
		if (g.m_state == GameState::Menu) g.m_menuScreen = MenuScreen::Main;
		else                              g.m_handbookOpen = false;
		g.m_shell.coverVisible = false;
		g.m_shell.previewId.clear();
		g.m_shell.title = "";
		return;
	}

	const float hintY = B.y + B.h * 0.5f - ui::kCharHNdc * 0.65f * 0.5f;
	const char* hint =
		"[Click] Select   [Left/Right] Group   [Up/Down] Entry   [Esc] Back";
	float hintW = ui::textWidthNdc(std::strlen(hint), 0.65f);
	ui::writeText(R, hint, B.x + B.w - hintW - 0.028f, hintY, 0.65f, kTextDim);

	// ── Preview area: voice detail card ─────────────────────────────────
	if (!selectedVoice.empty()) {
		const std::string& voice = selectedVoice;

		const float cardW = 0.60f;
		const float cardH = 0.34f;
		const float cardX = P.x + 0.040f;
		const float cardY = P.y + 0.030f;
		const float cardFill[4]   = {0.07f, 0.06f, 0.06f, 0.72f};
		const float cardShadow[4] = {0.00f, 0.00f, 0.00f, 0.40f};
		const float brass[4]      = {0.72f, 0.54f, 0.22f, 0.90f};
		ui::drawShadowPanel(R, cardX, cardY, cardW, cardH,
			cardShadow, cardFill, brass, 0.003f);

		ui::writeText(R, voice.c_str(), cardX + 0.022f,
			cardY + cardH - 0.052f, 1.00f, kText);
		ui::writeText(R, "voice : piper .onnx", cardX + 0.022f,
			cardY + cardH - 0.090f, 0.60f, kTextHint);

		std::string sample = "Hello, traveller. This is the voice of " + voice + ".";
		std::string line = "Sample: " + sample;
		size_t maxChars = (size_t)((cardW - 0.044f) /
			(ui::kCharWNdc * 0.65f));
		if (line.size() > maxChars) line = line.substr(0, maxChars);
		ui::writeText(R, line.c_str(), cardX + 0.022f,
			cardY + cardH - 0.132f, 0.65f, kTextDim);

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
			ui::writeText(R, u.c_str(), cardX + 0.022f,
				cardY + cardH - 0.170f, 0.60f, kTextDim);
		}

		const float pbW = 0.22f;
		const float pbH = 0.052f;
		const float pbX = cardX + 0.022f;
		const float pbY = cardY + 0.028f;
		auto now = std::chrono::steady_clock::now();
		bool cooldownActive =
			(now - m_lastVoiceSampleTime) < std::chrono::seconds(2);
		const char* btnLabel = cooldownActive ? "Synthesizing..." : "Play Sample";
		bool clicked = drawButton(R, pbX, pbY, pbW, pbH,
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
		ui::writeText(R, nm, cardX + 0.022f,
			cardY + cardH - 0.052f, 1.00f, kText);

		char idBuf[160];
		std::snprintf(idBuf, sizeof(idBuf), "%s : %s",
			selected->category.c_str(), selected->id.c_str());
		ui::writeText(R, idBuf, cardX + 0.022f,
			cardY + cardH - 0.090f, 0.60f, kTextHint);

		float fieldY = cardY + cardH - 0.132f;
		if (!selected->description.empty()) {
			size_t maxChars = (size_t)((cardW - 0.044f) /
				(ui::kCharWNdc * 0.65f));
			std::string desc = selected->description;
			if (desc.size() > maxChars) desc = desc.substr(0, maxChars);
			ui::writeText(R, desc.c_str(), cardX + 0.022f,
				fieldY, 0.65f, kTextDim);
			fieldY -= 0.038f;
		}

		if (!selected->subcategory.empty()) {
			char sb[96];
			std::snprintf(sb, sizeof(sb), "subcategory: %s",
				selected->subcategory.c_str());
			ui::writeText(R, sb, cardX + 0.022f, fieldY, 0.60f, kTextDim);
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
			ui::writeText(R, t.c_str(), cardX + 0.022f,
				fieldY, 0.60f, kTextDim);
		}
	}
}

} // namespace civcraft::vk

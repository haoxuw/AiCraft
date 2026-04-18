// Unified hotbar + inventory UI — custom-drawn, no ImGui.
//
// Three entry points on HudRenderer:
//   renderInventoryItems3D — 3D item preview for every recorded slot. Runs
//     in the main scene pass (before the 2D swapchain pass) so item box
//     models render WITH the world and depth-composite correctly against
//     the camera-aligned grey backing tiles emitted alongside them.
//   renderHotbarBar        — 2D bottom-center 10-slot strip. Always visible
//     in Playing state; records SlotRect{kind=Hotbar}.
//   renderInventoryPanel   — Tab-triggered OpenMW-inspired parchment panel.
//     Entirely drawRect2D + drawText2D (no ImGui window, no DrawList). Also
//     owns the drag-and-drop state machine: hit-test, press/release, drop
//     resolution, ghost rendering.
//
// Shared slot render. Both the hotbar strip and the inventory grid call
// `drawSlotFrame` for chrome (frame, rarity strip, count badge, keybind).
// Each records a SlotRect into m_slotRectsThis; next frame's 3D pass
// iterates them and draws the actual 3D item preview at the same NDC.
// One-frame lag is invisible — the UI doesn't move between frames.
//
// Drag-and-drop resolution matrix:
//   Inv → Hotbar[i]  : alias (set hotbar slot to itemId)
//   Hotbar[i] → Hotbar[j] : swap aliases
//   Hotbar[i] → void : clear hotbar slot
//   Inv → void       : TYPE_RELOCATE Self→Ground (drop to world)
//   Inv → Inv        : no-op (counter-based — physical position is
//                      meaningless)

#include "client/game_vk_renderers.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"

#include "client/box_model_flatten.h"
#include "client/box_model.h"
#include "logic/artifact_registry.h"
#include "logic/material_values.h"
#include "logic/entity.h"
#include "net/server_interface.h"
#include "logic/action.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// Geometry — kept local so the hotbar and inventory panel share one source
// of truth for slot sizing. NDC y grows upward (matches drawRect2D).
// ─────────────────────────────────────────────────────────────────────────
static constexpr int   kHotbarN    = 10;
static constexpr float kHotbarH    = 0.14f;    // NDC height
static constexpr float kHotbarGap  = 0.008f;
static constexpr float kHotbarYBot = -0.97f;

static constexpr int   kInvCols    = 8;
static constexpr int   kInvRows    = 6;

// Rarity color from material value — same scale the handbook uses.
static glm::vec4 rarityColor(float v) {
	// 0→gray, 1→white, 3→green, 8→blue, 16→purple, 32→gold.
	if (v >= 32.0f) return {1.00f, 0.82f, 0.25f, 1.0f};
	if (v >= 16.0f) return {0.72f, 0.45f, 0.95f, 1.0f};
	if (v >=  8.0f) return {0.35f, 0.65f, 0.95f, 1.0f};
	if (v >=  3.0f) return {0.45f, 0.85f, 0.40f, 1.0f};
	if (v >=  1.0f) return {0.92f, 0.90f, 0.80f, 1.0f};
	return                  {0.65f, 0.62f, 0.58f, 1.0f};
}

static std::string prettify(const std::string& id) {
	std::string s = id;
	auto colon = s.find(':');
	if (colon != std::string::npos) s = s.substr(colon + 1);
	for (auto& c : s) if (c == '_') c = ' ';
	if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
	return s;
}

// Point-in-rect using NDC (+Y up). Thin adapter over ui::rectContainsNdc
// so callers don't unpack SlotRect fields.
static bool rectContains(const Game::SlotRect& r, float x, float y) {
	return ui::rectContainsNdc(r.ndcX, r.ndcY, r.ndcW, r.ndcH, x, y);
}

// ─────────────────────────────────────────────────────────────────────────
// 3D pass — one drawBoxModel call for every slot rect recorded last frame.
// Grey backing tiles live in the SAME main-pass buffer so items composite
// on top via depth (fixing the "items float over raw world" bug).
// ─────────────────────────────────────────────────────────────────────────
void HudRenderer::renderInventoryItems3D() {
	Game& g = game_;
	const auto& slots = g.m_slotRectsLast;
	if (slots.empty()) return;

	glm::vec3 camPos   = g.m_cam.position;
	glm::vec3 camFwd   = glm::normalize(g.m_cam.front());
	glm::vec3 worldUp  = glm::vec3(0, 1, 0);
	if (std::abs(glm::dot(camFwd, worldUp)) > 0.99f) worldUp = glm::vec3(0, 0, 1);
	glm::vec3 camRight = glm::normalize(glm::cross(camFwd, worldUp));
	glm::vec3 camUp    = glm::normalize(glm::cross(camRight, camFwd));

	const float d      = 0.6f;
	const float fovRad = glm::radians(g.m_cam.fov);
	const float halfH  = d * std::tan(fovRad * 0.5f);
	const float halfW  = halfH * g.m_aspect;

	auto& boxes = g.m_scratch.charBoxes;
	boxes.clear();

	// Camera-aligned tile. Emits a thin camera-facing box at the slot's NDC
	// rect, pushed back along camFwd so a spinning item in front always
	// occludes it (no z-fighting, crisp silhouette).
	auto emitSlotTile = [&](float ndcX, float ndcY, float ndcW, float ndcH,
	                        glm::vec3 color, float depthPushBack) {
		float cx = ndcX + ndcW * 0.5f;
		float cy = ndcY + ndcH * 0.5f;
		glm::vec3 center = camPos + camFwd * d
		                 + camRight * (cx * halfW)
		                 + camUp    * (cy * halfH);
		float wW = ndcW * halfW;
		float hW = ndcH * halfH;
		float tk = 0.01f;
		center += camFwd * depthPushBack;
		glm::vec3 bx = camRight * wW;
		glm::vec3 by = camUp    * hW;
		glm::vec3 bz = camFwd   * tk;
		glm::vec3 corner = center - 0.5f * (bx + by + bz);
		glm::mat4 m(1.0f);
		m[0] = glm::vec4(bx, 0.0f);
		m[1] = glm::vec4(by, 0.0f);
		m[2] = glm::vec4(bz, 0.0f);
		m[3] = glm::vec4(corner, 1.0f);
		detail::emitBox(boxes, m, color);
	};

	const glm::vec3 kTileFilled{0.14f, 0.12f, 0.16f};
	const glm::vec3 kTileEmpty {0.10f, 0.09f, 0.12f};
	const glm::vec3 kTileSel   {0.42f, 0.30f, 0.10f};

	// Pass 1: grey backing tiles for inventory grid slots only. Hotbar slots
	// and drag-ghosts float free so they don't stamp solid squares over the
	// world.
	for (const auto& s : slots) {
		if (s.kind != Game::SlotRect::Kind::Inventory) continue;
		glm::vec3 col = (s.itemId.empty() || s.count <= 0)
		    ? kTileEmpty
		    : (s.selected ? kTileSel : kTileFilled);
		float pushBack = s.ndcH * halfH * 0.45f;
		emitSlotTile(s.ndcX, s.ndcY, s.ndcW, s.ndcH, col, pushBack);
	}

	// Pass 2: items.
	for (const auto& s : slots) {
		if (s.itemId.empty() || s.count <= 0) continue;

		std::string stem = s.itemId;
		auto colon = stem.find(':');
		if (colon != std::string::npos) stem = stem.substr(colon + 1);
		auto it = g.m_models.find(stem);
		if (it == g.m_models.end()) {
			it = g.m_models.find(s.itemId);
			if (it == g.m_models.end()) continue;
		}

		float slotCx = s.ndcX + s.ndcW * 0.5f;
		float slotCy = s.ndcY + s.ndcH * 0.5f;
		glm::vec3 itemWorldPos = camPos + camFwd * d
		                       + camRight * (slotCx * halfW)
		                       + camUp    * (slotCy * halfH);

		float slotWorldH   = s.ndcH * halfH;
		float targetWorldH = slotWorldH * 0.44f;

		civcraft::BoxModel m = it->second;

		float s0 = m.modelScale;
		float minX =  1e9f, maxX = -1e9f;
		float minY =  1e9f, maxY = -1e9f;
		float minZ =  1e9f, maxZ = -1e9f;
		for (auto& part : m.parts) {
			glm::vec3 o = part.offset   * s0;
			glm::vec3 hs= part.halfSize * s0;
			minX = std::min(minX, o.x - hs.x); maxX = std::max(maxX, o.x + hs.x);
			minY = std::min(minY, o.y - hs.y); maxY = std::max(maxY, o.y + hs.y);
			minZ = std::min(minZ, o.z - hs.z); maxZ = std::max(maxZ, o.z + hs.z);
		}
		float dy = std::max(maxY - minY, 0.01f);
		float dx = std::max(maxX - minX, 0.01f);
		float dz = std::max(maxZ - minZ, 0.01f);
		float maxDim = std::max(dy, std::max(dx, dz));
		float scale = targetWorldH / maxDim;
		for (auto& part : m.parts) {
			part.offset   *= scale;
			part.halfSize *= scale;
		}
		float cy = (minY + maxY) * 0.5f * scale;

		civcraft::AnimState anim{};
		anim.time = g.m_wallTime;
		anim.suppressIdleBob = true;
		float rpm     = s.selected ? 80.0f : 32.0f;
		float offset  = (float)((size_t)(&s - slots.data()) * 37);
		float slowSpin = (float)g.m_wallTime * rpm + offset;

		// Camera-basis root: local +X = camRight, +Y = camUp, +Z = -camFwd
		// (toward the camera). This pins the slot item upright in the slot
		// at any camera pitch/yaw, not just yaw. Then spin around local +Y
		// for the in-place rotation, and -Y * cy to centre vertically.
		glm::mat4 root(1.0f);
		root[0] = glm::vec4(camRight, 0.0f);
		root[1] = glm::vec4(camUp,    0.0f);
		root[2] = glm::vec4(-camFwd,  0.0f);
		root[3] = glm::vec4(itemWorldPos, 1.0f);
		root = glm::rotate(root, glm::radians(-slowSpin - 90.0f), glm::vec3(0, 1, 0));
		root = glm::translate(root, glm::vec3(0.0f, -cy, 0.0f));

		civcraft::appendBoxModel(boxes, m, glm::vec3(0.0f), 0.0f, anim, nullptr, &root);
	}

	if (boxes.empty()) return;

	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = g.viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float) * 16);
	scene.camPos[0] = camPos.x; scene.camPos[1] = camPos.y; scene.camPos[2] = camPos.z;
	scene.time = g.m_wallTime;
	glm::vec3 sun = glm::normalize(glm::vec3(0.35f, 0.90f, 0.45f));
	scene.sunDir[0] = sun.x; scene.sunDir[1] = sun.y; scene.sunDir[2] = sun.z;
	scene.sunStr = 0.95f;

	g.m_rhi->drawBoxModel(scene, boxes.data(), (uint32_t)(boxes.size() / 19));
}

// ─────────────────────────────────────────────────────────────────────────
// Slot chrome — shared by hotbar + inventory grid. `hover` lights up the
// outline in brass; `selected` paints a full accent box (hotbar selection).
// ─────────────────────────────────────────────────────────────────────────
namespace {
struct SlotChromeArgs {
	float x, y, w, h;         // NDC
	const std::string* itemId;
	int  count;
	bool selected;
	bool hover;
	const char* keyLabel;     // e.g. "1" on hotbar; nullptr for grid cells
};

void drawSlotFrame(rhi::IRhi* r, const SlotChromeArgs& a) {
	const bool hasItem = a.itemId && !a.itemId->empty() && a.count > 0;

	// Base fill — empty cells get a darker translucent back so absence reads.
	// Filled cells skip the fill (3D item + backing tile handle the look).
	const float bgEmpty[4] = {0.08f, 0.07f, 0.10f, 0.45f};
	if (!hasItem) r->drawRect2D(a.x, a.y, a.w, a.h, bgEmpty);

	// Selection highlight (hotbar active slot).
	if (a.selected) {
		const float selTint[4] = {0.95f, 0.75f, 0.25f, 0.18f};
		r->drawRect2D(a.x, a.y, a.w, a.h, selTint);
	}

	// Outline: brass when selected/hovered, dim otherwise.
	const float outSel  [4] = {0.95f, 0.75f, 0.25f, 0.95f};
	const float outHover[4] = {0.95f, 0.82f, 0.45f, 0.90f};
	const float outDim  [4] = {0.38f, 0.28f, 0.14f, 0.85f};
	const float* out = a.selected ? outSel : (a.hover ? outHover : outDim);
	float t = a.selected ? 0.004f : 0.002f;
	ui::drawOutline(r, a.x, a.y, a.w, a.h, t, out);

	// Count chip bottom-right (no rarity strip — user prefers a clean slot).
	if (hasItem) {
		if (a.count > 1) {
			char cnt[8]; std::snprintf(cnt, sizeof(cnt), "x%d", a.count);
			size_t cntLen = std::strlen(cnt);
			float chipW = 0.012f + 0.011f * (float)cntLen;
			float chipH = 0.024f;
			float chipX = a.x + a.w - chipW - 0.004f;
			float chipY = a.y + 0.004f;
			const float badge[4] = {0.04f, 0.03f, 0.05f, 0.92f};
			r->drawRect2D(chipX, chipY, chipW, chipH, badge);
			const float wh[4] = {1.0f, 0.97f, 0.85f, 1.0f};
			r->drawText2D(cnt, chipX + 0.004f, chipY + 0.005f, 0.62f, wh);
		}
	}

	// Keybind label (hotbar only).
	if (a.keyLabel) {
		const float keyDim   [4] = {0.55f, 0.50f, 0.42f, 0.80f};
		const float keyBright[4] = {0.98f, 0.88f, 0.55f, 0.98f};
		r->drawText2D(a.keyLabel, a.x + 0.006f, a.y + a.h - 0.028f,
		              0.58f, a.selected ? keyBright : keyDim);
	}
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Hotbar bar — always visible while playing.
// ─────────────────────────────────────────────────────────────────────────
void HudRenderer::renderHotbarBar() {
	Game& g = game_;
	civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());
	if (!me || !me->inventory) return;

	const float slotW = kHotbarH / g.m_aspect;
	float totalW = kHotbarN * slotW + (kHotbarN - 1) * kHotbarGap;
	float x0 = -totalW * 0.5f;
	float y0 = kHotbarYBot;

	for (int i = 0; i < kHotbarN; i++) {
		float x = x0 + i * (slotW + kHotbarGap);
		const std::string& id = g.m_hotbar.get(i);
		int cnt = g.m_hotbar.count(i, *me->inventory);
		bool sel = (i == g.m_hotbar.selected);

		Game::SlotRect sr;
		sr.ndcX = x; sr.ndcY = y0; sr.ndcW = slotW; sr.ndcH = kHotbarH;
		sr.itemId = id; sr.count = cnt; sr.selected = sel;
		sr.kind = Game::SlotRect::Kind::Hotbar;
		sr.index = i;
		g.m_slotRectsThis.push_back(sr);

		// Hover lookup via last-frame rect at this slot index.
		bool hover = false;
		if (g.m_hoverSlot >= 0 && g.m_hoverSlot < (int)g.m_slotRectsLast.size()) {
			const auto& h = g.m_slotRectsLast[g.m_hoverSlot];
			hover = (h.kind == Game::SlotRect::Kind::Hotbar && h.index == i);
		}

		char lab[4]; std::snprintf(lab, sizeof(lab), "%d", (i + 1) % 10);
		SlotChromeArgs a{x, y0, slotW, kHotbarH, &id, cnt, sel, hover, lab};
		drawSlotFrame(g.m_rhi, a);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// Inventory panel — custom-drawn parchment. All geometry in NDC.
// ─────────────────────────────────────────────────────────────────────────
void HudRenderer::renderInventoryPanel() {
	Game& g = game_;

	// Drag auto-cancels if the panel closes mid-drag — nothing to resolve.
	if (!g.m_invOpen) {
		if (g.m_drag.active) g.m_drag = {};
		g.m_hoverSlot = -1;
		return;
	}

	civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());
	if (!me || !me->inventory) return;

	rhi::IRhi* r = g.m_rhi;

	// ── Hit-test cursor against last frame's rects ────────────────────
	g.m_hoverSlot = -1;
	for (size_t i = 0; i < g.m_slotRectsLast.size(); i++) {
		const auto& sr = g.m_slotRectsLast[i];
		if (sr.kind == Game::SlotRect::Kind::DragGhost) continue;
		if (rectContains(sr, g.m_mouseNdcX, g.m_mouseNdcY)) {
			g.m_hoverSlot = (int)i;
			break;
		}
	}

	// ── Drag state machine ────────────────────────────────────────────
	if (g.m_mouseLPressed && !g.m_drag.active && g.m_hoverSlot >= 0) {
		const auto& src = g.m_slotRectsLast[g.m_hoverSlot];
		if (!src.itemId.empty() && src.count > 0) {
			g.m_drag.active   = true;
			g.m_drag.itemId   = src.itemId;
			g.m_drag.count    = src.count;
			g.m_drag.srcKind  = src.kind;
			g.m_drag.srcIndex = src.index;
		}
	}
	if (g.m_mouseLReleased && g.m_drag.active) {
		const Game::SlotRect* tgt = nullptr;
		if (g.m_hoverSlot >= 0) tgt = &g.m_slotRectsLast[g.m_hoverSlot];
		// Don't count self-on-self as a drop.
		if (tgt && tgt->kind == g.m_drag.srcKind && tgt->index == g.m_drag.srcIndex)
			tgt = nullptr;

		// Drop resolution matrix, inlined so the friend relationship (HudRenderer
		// is friend of Game) gives access to private m_hotbar / m_server. A free
		// helper would have no such access.
		using K = Game::SlotRect::Kind;
		auto saveHotbar = [&]() {
			if (!g.m_hotbarSavePath.empty())
				g.m_hotbar.saveToFile(g.m_hotbarSavePath);
		};
		if (tgt && tgt->kind == K::Hotbar) {
			if (g.m_drag.srcKind == K::Hotbar) {
				std::string a = g.m_hotbar.get(g.m_drag.srcIndex);
				std::string b = g.m_hotbar.get(tgt->index);
				g.m_hotbar.set(g.m_drag.srcIndex, b);
				g.m_hotbar.set(tgt->index, a);
			} else {
				g.m_hotbar.set(tgt->index, g.m_drag.itemId);
			}
			saveHotbar();
		} else if (!tgt) {
			if (g.m_drag.srcKind == K::Hotbar) {
				g.m_hotbar.clear(g.m_drag.srcIndex);
				saveHotbar();
			} else if (g.m_drag.srcKind == K::Inventory) {
				// Drop one to the world (TYPE_RELOCATE Self → Ground).
				civcraft::ActionProposal p;
				p.type         = civcraft::ActionProposal::Relocate;
				p.actorId      = g.m_server->localPlayerId();
				p.relocateFrom = civcraft::Container::self();
				p.relocateTo   = civcraft::Container::ground();
				p.itemId       = g.m_drag.itemId;
				p.itemCount    = 1;
				g.m_server->sendAction(p);
			}
		}
		// Inv → Inv: no-op (counter-based; physical position is meaningless).

		g.m_drag = {};
	}

	// ── Sort the player inventory ─────────────────────────────────────
	auto items = me->inventory->items();
	auto sortItems = [&](std::vector<std::pair<std::string,int>>& v) {
		switch (g.m_invSort) {
		case Game::InvSort::ByValue:
			std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){
				std::string ra = a.first, rb = b.first;
				auto ca = ra.find(':'); if (ca != std::string::npos) ra = ra.substr(ca+1);
				auto cb = rb.find(':'); if (cb != std::string::npos) rb = rb.substr(cb+1);
				float va = civcraft::getMaterialValue(ra);
				float vb = civcraft::getMaterialValue(rb);
				if (va != vb) return va > vb;
				return a.first < b.first;
			});
			break;
		case Game::InvSort::ByCount:
			std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){
				if (a.second != b.second) return a.second > b.second;
				return a.first < b.first;
			});
			break;
		case Game::InvSort::ByName:
		default:
			std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){
				return a.first < b.first;
			});
			break;
		}
	};
	sortItems(items);

	// ── Panel geometry ────────────────────────────────────────────────
	// Parchment dimensions in NDC. Fixed NDC width scales with aspect (wider
	// screens show a wider panel but cells stay square-ish). Height fixed.
	const float panelW = 1.40f;                      // ~70% of screen
	const float panelH = 1.60f;                      // ~80% of screen
	const float panelX = -panelW * 0.5f;
	const float panelY = -panelH * 0.5f;

	// Shadow (offset down-right), outer dark frame, warm panel fill,
	// inner highlight at top.
	//
	// Grid-area transparency: the 3D pass renders item box-models BEFORE
	// the 2D pass runs, so an opaque panel fill would completely obscure
	// the items. We paint the fill as 4 bands (top/bottom/left/right)
	// around the grid area. The grid stays transparent so 3D items and
	// their grey backing tiles composite through unobstructed.
	const float shadow [4] = {0.00f, 0.00f, 0.00f, 0.40f};
	const float frameOut[4]= {0.08f, 0.06f, 0.04f, 0.98f};
	const float fill   [4] = {0.11f, 0.09f, 0.08f, 0.96f};
	const float brass  [4] = {0.65f, 0.48f, 0.20f, 1.00f};
	const float brassHi[4] = {0.95f, 0.78f, 0.35f, 1.00f};

	// Grid-area bounds reserved up-front so the bands can wrap them.
	const float gridReserveTop    = 0.300f;   // title+tabs+stats band height
	const float gridReserveBottom = 0.090f;   // footer band height
	const float gridReserveSide   = 0.045f;   // side bands width
	const float gridBandTopY      = panelY + panelH - gridReserveTop;
	const float gridBandBotY      = panelY + gridReserveBottom;
	const float gridBandLeftX     = panelX + gridReserveSide;
	const float gridBandRightX    = panelX + panelW - gridReserveSide;
	const float gridBandW         = gridBandRightX - gridBandLeftX;
	const float gridBandH         = gridBandTopY   - gridBandBotY;

	// Full-panel shadow — soft, alpha low so items underneath still read.
	r->drawRect2D(panelX + 0.015f, panelY - 0.020f, panelW, panelH, shadow);
	// Outer dark frame as 4 thin edges (leaves the center transparent).
	{
		float ft = 0.008f;
		ui::drawOutline(r, panelX - ft, panelY - ft,
			panelW + 2 * ft, panelH + 2 * ft, ft, frameOut);
	}
	// Dark fill bands — top (title+tabs+stats), bottom (footer), sides.
	r->drawRect2D(panelX, gridBandTopY, panelW, gridReserveTop, fill);
	r->drawRect2D(panelX, panelY,       panelW, gridReserveBottom, fill);
	r->drawRect2D(panelX,         gridBandBotY, gridReserveSide, gridBandH, fill);
	r->drawRect2D(gridBandRightX, gridBandBotY, gridReserveSide, gridBandH, fill);

	// Brass inset border — 4 thin edges just inside the panel bounds.
	{
		float t = 0.003f;
		float off = 0.010f;
		float bx = panelX + off, by = panelY + off;
		float bw = panelW - off * 2, bh = panelH - off * 2;
		ui::drawOutline(r, bx, by, bw, bh, t, brass);
		// Upper highlight (1-px warm line just above the top brass edge).
		r->drawRect2D(bx + t, by + bh - t - 0.002f, bw - 2 * t, 0.002f, brassHi);
		// Inner brass edges framing the grid window.
		r->drawRect2D(gridBandLeftX - t, gridBandBotY, t, gridBandH, brass);
		r->drawRect2D(gridBandRightX,    gridBandBotY, t, gridBandH, brass);
		r->drawRect2D(gridBandLeftX - t, gridBandTopY, gridBandW + 2 * t, t, brass);
		r->drawRect2D(gridBandLeftX - t, gridBandBotY - t, gridBandW + 2 * t, t, brass);
	}
	(void)gridBandW;

	// ── Title strip (top of panel) ────────────────────────────────────
	const float titleY = panelY + panelH - 0.13f;
	const float titleStrip[4] = {0.16f, 0.12f, 0.09f, 0.95f};
	r->drawRect2D(panelX + 0.018f, titleY, panelW - 0.036f, 0.10f, titleStrip);

	const float titleCol[4] = {1.0f, 0.85f, 0.45f, 1.0f};
	// Crude centering: title width in NDC ≈ 0.11 per character at scale 1.4 / aspect.
	const char* titleStr = "INVENTORY";
	float titleScale = 1.35f;
	float titleCharW = 0.020f * titleScale / g.m_aspect;
	float titleW = std::strlen(titleStr) * titleCharW;
	r->drawTitle2D(titleStr,
	               -titleW * 0.5f,
	               titleY + 0.028f,
	               titleScale, titleCol);

	// ── Sort tabs (below title) ───────────────────────────────────────
	const float tabY   = titleY - 0.065f;
	const float tabH   = 0.050f;
	const float tabGap = 0.010f;
	const float tabW   = 0.20f;
	struct Tab { const char* label; Game::InvSort mode; };
	const Tab tabs[3] = {
		{ "By Name",  Game::InvSort::ByName  },
		{ "By Value", Game::InvSort::ByValue },
		{ "By Count", Game::InvSort::ByCount },
	};
	float tabsTotalW = 3 * tabW + 2 * tabGap;
	float tabsX0 = -tabsTotalW * 0.5f;
	for (int i = 0; i < 3; i++) {
		float tx = tabsX0 + i * (tabW + tabGap);
		bool active = (g.m_invSort == tabs[i].mode);
		bool hover = g.m_mouseNdcX >= tx && g.m_mouseNdcX <= tx + tabW
		          && g.m_mouseNdcY >= tabY && g.m_mouseNdcY <= tabY + tabH;

		const float fillOff [4] = {0.18f, 0.14f, 0.10f, 0.95f};
		const float fillOn  [4] = {0.42f, 0.30f, 0.12f, 0.98f};
		const float fillHov [4] = {0.28f, 0.21f, 0.12f, 0.96f};
		r->drawRect2D(tx, tabY, tabW, tabH,
		              active ? fillOn : (hover ? fillHov : fillOff));

		// Top edge highlight on active tab.
		if (active) {
			const float topLit[4] = {1.0f, 0.82f, 0.35f, 1.0f};
			r->drawRect2D(tx, tabY + tabH - 0.003f, tabW, 0.003f, topLit);
		}
		const float brd[4] = {0.35f, 0.25f, 0.10f, 0.95f};
		ui::drawOutline(r, tx, tabY, tabW, tabH, 0.0015f, brd);

		const float labelCol[4]  = {0.94f, 0.88f, 0.70f, 1.0f};
		const float labelDim [4] = {0.70f, 0.62f, 0.50f, 1.0f};
		float labScale = 0.75f;
		float labCharW = 0.013f * labScale / g.m_aspect;
		float labW = std::strlen(tabs[i].label) * labCharW;
		r->drawText2D(tabs[i].label,
		              tx + tabW * 0.5f - labW * 0.5f,
		              tabY + 0.014f,
		              labScale, active ? labelCol : labelDim);

		// Click: cycle on this tab.
		if (hover && g.m_mouseLPressed && !g.m_drag.active) {
			g.m_invSort = tabs[i].mode;
		}
	}

	// ── Stats row (below tabs) ────────────────────────────────────────
	{
		char stats[160];
		int totalCount = 0;
		for (auto& p : items) totalCount += p.second;
		float mass = me->inventory->totalValue();
		std::snprintf(stats, sizeof(stats),
		              "%d items    %d stacks    %.1f mass",
		              totalCount, (int)items.size(), mass);
		const float dim[4] = {0.68f, 0.62f, 0.54f, 1.0f};
		float statsScale = 0.68f;
		float statsCharW = 0.013f * statsScale / g.m_aspect;
		float statsW = std::strlen(stats) * statsCharW;
		r->drawText2D(stats,
		              -statsW * 0.5f, tabY - 0.040f,
		              statsScale, dim);
	}

	// ── 8×6 Grid of slots ─────────────────────────────────────────────
	// Live inside the transparent window between the dark bands so 3D
	// items and grey backing tiles show through.
	const float gridTop     = gridBandTopY - 0.010f;
	const float gridBottomY = gridBandBotY + 0.010f;
	const float gridH       = gridTop - gridBottomY;

	const float cellPad = 0.008f;
	float cellH = (gridH - cellPad * (kInvRows - 1)) / kInvRows;
	if (cellH > 0.14f) cellH = 0.14f;
	float cellW = cellH / g.m_aspect;
	// If cells are too narrow for the panel, widen them while keeping square-ish.
	float gridW = cellW * kInvCols + cellPad * (kInvCols - 1);
	float gridX0 = -gridW * 0.5f;
	float gridY0 = gridBottomY + (gridH - (cellH * kInvRows + cellPad * (kInvRows - 1)));

	int maxVisible = kInvCols * kInvRows;
	for (int row = 0; row < kInvRows; row++) {
		for (int col = 0; col < kInvCols; col++) {
			int idx = row * kInvCols + col;
			float cx = gridX0 + col * (cellW + cellPad);
			// Rows render top-down visually → y = top - row*(cellH+pad).
			float cy = gridY0 + (kInvRows - 1 - row) * (cellH + cellPad);

			std::string slotId;
			int         slotCount = 0;
			if (idx < (int)items.size()) {
				slotId    = items[idx].first;
				slotCount = items[idx].second;
			}

			Game::SlotRect sr;
			sr.ndcX = cx; sr.ndcY = cy; sr.ndcW = cellW; sr.ndcH = cellH;
			sr.itemId = slotId;
			sr.count  = slotCount;
			sr.selected = false;
			sr.kind  = Game::SlotRect::Kind::Inventory;
			sr.index = idx;
			g.m_slotRectsThis.push_back(sr);

			bool hover = false;
			if (g.m_hoverSlot >= 0 && g.m_hoverSlot < (int)g.m_slotRectsLast.size()) {
				const auto& h = g.m_slotRectsLast[g.m_hoverSlot];
				hover = (h.kind == Game::SlotRect::Kind::Inventory && h.index == idx);
			}

			SlotChromeArgs a{cx, cy, cellW, cellH,
			                 &slotId, slotCount, false, hover, nullptr};
			drawSlotFrame(r, a);

			(void)maxVisible;
		}
	}

	// ── Footer hint ───────────────────────────────────────────────────
	{
		char hint[200];
		std::snprintf(hint, sizeof(hint),
		              "Drag to hotbar %d    Drag out to drop    ESC / Tab to close    Q = drop held",
		              (g.m_hotbar.selected + 1) % 10);
		const float dim[4] = {0.58f, 0.52f, 0.46f, 1.0f};
		float hScale = 0.62f;
		float hCharW = 0.013f * hScale / g.m_aspect;
		float hW = std::strlen(hint) * hCharW;
		r->drawText2D(hint,
		              -hW * 0.5f, panelY + 0.028f,
		              hScale, dim);
	}

	// ── Drag ghost (follows cursor) ───────────────────────────────────
	if (g.m_drag.active) {
		float ghostH = kHotbarH;
		float ghostW = ghostH / g.m_aspect;
		Game::SlotRect gh;
		gh.ndcX = g.m_mouseNdcX - ghostW * 0.5f;
		gh.ndcY = g.m_mouseNdcY - ghostH * 0.5f;
		gh.ndcW = ghostW;
		gh.ndcH = ghostH;
		gh.itemId = g.m_drag.itemId;
		gh.count  = g.m_drag.count;
		gh.selected = false;
		gh.kind  = Game::SlotRect::Kind::DragGhost;
		gh.index = -1;
		g.m_slotRectsThis.push_back(gh);

		// Faint count chip on the ghost so it reads when the 3D model is thin.
		if (g.m_drag.count > 1) {
			char cnt[16];
			std::snprintf(cnt, sizeof(cnt), "x%d", g.m_drag.count);
			const float badge[4] = {0.04f, 0.03f, 0.05f, 0.92f};
			float chipW = 0.014f + 0.011f * (float)std::strlen(cnt);
			float chipH = 0.026f;
			float chipX = gh.ndcX + ghostW - chipW - 0.004f;
			float chipY = gh.ndcY + 0.004f;
			r->drawRect2D(chipX, chipY, chipW, chipH, badge);
			const float wh[4] = {1.0f, 0.97f, 0.85f, 1.0f};
			r->drawText2D(cnt, chipX + 0.004f, chipY + 0.005f, 0.62f, wh);
		}
	}

	// ── Hover tooltip (only when not dragging) ────────────────────────
	if (!g.m_drag.active && g.m_hoverSlot >= 0) {
		const auto& h = g.m_slotRectsLast[g.m_hoverSlot];
		if (!h.itemId.empty() && h.count > 0) {
			std::string raw = h.itemId;
			auto colon = raw.find(':');
			if (colon != std::string::npos) raw = raw.substr(colon + 1);
			glm::vec4 rc = rarityColor(civcraft::getMaterialValue(raw));

			std::string name = prettify(h.itemId);
			char line2[96];
			std::snprintf(line2, sizeof(line2), "x%d    %.1f value",
			              h.count, civcraft::getMaterialValue(raw));

			float sName = 0.78f, sMeta = 0.62f;
			float nameCharW = 0.013f * sName / g.m_aspect;
			float metaCharW = 0.013f * sMeta / g.m_aspect;
			float nameW = name.size() * nameCharW;
			float metaW = std::strlen(line2) * metaCharW;
			float tipW = std::max(nameW, metaW) + 0.030f;
			float tipH = 0.086f;

			// Offset up-right from cursor, clamp to screen.
			float tx = g.m_mouseNdcX + 0.020f;
			float ty = g.m_mouseNdcY + 0.020f;
			if (tx + tipW > 0.98f) tx = g.m_mouseNdcX - tipW - 0.020f;
			if (ty + tipH > 0.98f) ty = g.m_mouseNdcY - tipH - 0.020f;

			const float tipShadow[4] = {0.0f, 0.0f, 0.0f, 0.55f};
			const float tipFill  [4] = {0.09f, 0.07f, 0.06f, 0.98f};
			const float tipBorder[4] = {0.55f, 0.40f, 0.18f, 1.0f};
			r->drawRect2D(tx + 0.008f, ty - 0.008f, tipW, tipH, tipShadow);
			r->drawRect2D(tx, ty, tipW, tipH, tipFill);
			ui::drawOutline(r, tx, ty, tipW, tipH, 0.002f, tipBorder);

			const float nameColArr[4] = { rc.x, rc.y, rc.z, 1.0f };
			const float metaCol[4]    = {0.70f, 0.64f, 0.54f, 1.0f};
			r->drawText2D(name.c_str(), tx + 0.012f, ty + tipH - 0.030f,
			              sName, nameColArr);
			r->drawText2D(line2,         tx + 0.012f, ty + 0.014f,
			              sMeta, metaCol);
		}
	}
}

} // namespace civcraft::vk

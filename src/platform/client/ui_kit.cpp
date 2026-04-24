#include "client/ui_kit.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <unordered_map>

namespace civcraft::vk::ui {

namespace color {
const float kBrass[4]     = { 0.96f, 0.82f, 0.40f, 1.00f };
const float kRed[4]       = { 1.00f, 0.35f, 0.30f, 1.00f };
const float kText[4]      = { 0.92f, 0.90f, 0.88f, 1.00f };
const float kTextDim[4]   = { 0.65f, 0.62f, 0.60f, 0.90f };
const float kTextHint[4]  = { 0.55f, 0.55f, 0.60f, 0.85f };
const float kDanger[4]    = { 0.95f, 0.35f, 0.30f, 1.00f };
const float kScrim[4]     = { 0.00f, 0.00f, 0.00f, 0.55f };
const float kScrimDark[4] = { 0.00f, 0.00f, 0.00f, 0.68f };
const float kRowBg[4]     = { 0.08f, 0.08f, 0.10f, 0.55f };
const float kSelBg[4]     = { 0.30f, 0.55f, 0.85f, 0.55f };
}

void drawOutline(rhi::IRhi* r, float x, float y, float w, float h,
                 float t, const float color[4]) {
	r->drawRect2D(x,         y,         w, t, color);
	r->drawRect2D(x,         y + h - t, w, t, color);
	r->drawRect2D(x,         y,         t, h, color);
	r->drawRect2D(x + w - t, y,         t, h, color);
}

void drawShadowPanel(rhi::IRhi* r, float x, float y, float w, float h,
                     const float shadow[4], const float fill[4],
                     const float border[4], float borderT) {
	if (shadow) r->drawRect2D(x + 0.010f, y - 0.014f, w, h, shadow);
	if (fill)   r->drawRect2D(x, y, w, h, fill);
	if (border) drawOutline(r, x, y, w, h, borderT, border);
}

void drawMeter(rhi::IRhi* r, float x, float y, float w, float h,
               float frac, const float fill[4], const float bg[4],
               const float border[4]) {
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	r->drawRect2D(x, y, w, h, bg);
	r->drawRect2D(x, y, w * frac, h, fill);
	if (border) drawOutline(r, x, y, w, h, 0.0015f, border);
}

void drawCenteredText(rhi::IRhi* r, const char* txt, float cx, float y,
                      float scale, const float color[4]) {
	float wNdc = textWidthNdc(std::strlen(txt), scale);
	r->drawText2D(txt, cx - wNdc * 0.5f, y, scale, color);
}

void drawCenteredTitle(rhi::IRhi* r, const char* txt, float cx, float y,
                       float scale, const float color[4]) {
	float wNdc = textWidthNdc(std::strlen(txt), scale);
	r->drawTitle2D(txt, cx - wNdc * 0.5f, y, scale, color);
}

void writeText(rhi::IRhi* r, const char* txt, float x, float y,
               float scale, const float color[4], TextAlign align) {
	if (!txt || !*txt) return;
	float anchorX = x;
	if (align != TextAlign::Left) {
		float wNdc = textWidthNdc(std::strlen(txt), scale);
		if      (align == TextAlign::Center) anchorX = x - wNdc * 0.5f;
		else /* Right */                     anchorX = x - wNdc;
	}
	// Always the title mode — that's the font the floaters use and what
	// callers asked for: outlined, bloom-friendly, legible at any scale.
	r->drawTitle2D(txt, anchorX, y, scale, color);
}

bool keyEdge(GLFWwindow* w, int key) {
	static std::unordered_map<int, int> lastState;
	int cur = glfwGetKey(w, key);
	int prev = lastState[key];
	lastState[key] = cur;
	return cur == GLFW_PRESS && prev != GLFW_PRESS;
}

// ── Peekable text ─────────────────────────────────────────────────────

bool findCoord(const std::string& s, size_t from, CoordMatch& out) {
	for (size_t i = from; i < s.size(); ++i) {
		if (s[i] != '(') continue;
		size_t p = i + 1;
		auto skipWs = [&]() { while (p < s.size() && s[p] == ' ') ++p; };
		auto readInt = [&](int& v) -> bool {
			skipWs();
			size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			size_t digStart = p;
			while (p < s.size() && s[p] >= '0' && s[p] <= '9') ++p;
			if (p == digStart) return false;
			try { v = std::stoi(s.substr(start, p - start)); }
			catch (...) { return false; }
			return true;
		};
		int x, y, z;
		if (!readInt(x)) continue;
		skipWs(); if (p >= s.size() || s[p] != ',') continue; ++p;
		if (!readInt(y)) continue;
		skipWs(); if (p >= s.size() || s[p] != ',') continue; ++p;
		if (!readInt(z)) continue;
		skipWs(); if (p >= s.size() || s[p] != ')') continue;
		out.begin = i;
		out.end   = p + 1;
		out.coord = {x, y, z};
		return true;
	}
	return false;
}

float drawPeekableText(rhi::IRhi* r, const PeekableTextInput& in,
                       float x, float y, float scale,
                       const std::string& value, const float baseCol[4]) {
	// Cyan link palette — fixed so the "clickable coord" affordance is
	// recognisable across every surface that renders peekable text.
	static const float kLinkCol[4] = {0.55f, 0.80f, 1.00f, 1.0f};
	static const float kLinkHot[4] = {0.85f, 0.95f, 1.00f, 1.0f};

	float xCur = x;
	size_t cursor = 0;
	CoordMatch cm;
	while (findCoord(value, cursor, cm)) {
		if (cm.begin > cursor) {
			std::string lit = value.substr(cursor, cm.begin - cursor);
			r->drawText2D(lit.c_str(), xCur, y, scale, baseCol);
			xCur += textWidthNdc(lit.size(), scale);
		}
		std::string coordStr = value.substr(cm.begin, cm.end - cm.begin);
		float coordW = textWidthNdc(coordStr.size(), scale);
		bool hover = rectContainsNdc(xCur, y, coordW, 0.026f,
		                             in.mouseNdcX, in.mouseNdcY);
		const float* col = hover ? kLinkHot : kLinkCol;
		r->drawText2D(coordStr.c_str(), xCur, y, scale, col);
		r->drawRect2D(xCur, y - 0.0025f, coordW, 0.0015f, col);
		if (hover && in.mouseLPressed && in.onCoordClick) {
			in.onCoordClick(cm.coord);
		}
		xCur += coordW;
		cursor = cm.end;
	}
	if (cursor < value.size()) {
		std::string tail = value.substr(cursor);
		r->drawText2D(tail.c_str(), xCur, y, scale, baseCol);
		xCur += textWidthNdc(tail.size(), scale);
	}
	return xCur - x;
}

} // namespace civcraft::vk::ui

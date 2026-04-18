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

bool keyEdge(GLFWwindow* w, int key) {
	static std::unordered_map<int, int> lastState;
	int cur = glfwGetKey(w, key);
	int prev = lastState[key];
	lastState[key] = cur;
	return cur == GLFW_PRESS && prev != GLFW_PRESS;
}

} // namespace civcraft::vk::ui

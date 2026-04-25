#include "client/action_icon.h"

#include "client/rhi/rhi.h"

#include <cmath>

namespace civcraft::vk {

namespace {

// Logical icon space — each kind's rect coords are written as if the
// icon spans [-1, +1] on both axes, with center at (0, 0). The drawer
// scales these into NDC using halfSize (Y) and halfSize/aspect (X) so
// the icon stays visually square regardless of screen ratio.
struct LogicalRect {
	float lx, ly;        // logical center
	float halfLW, halfLH;
};

void emitRect(rhi::IRhi* r, float cx, float cy, float halfSize, float aspect,
              const LogicalRect& lr, const float rgba[4]) {
	const float xUnit = halfSize / aspect;
	const float yUnit = halfSize;
	const float ndcCx = cx + lr.lx * xUnit;
	const float ndcCy = cy + lr.ly * yUnit;
	const float halfW = lr.halfLW * xUnit;
	const float halfH = lr.halfLH * yUnit;
	r->drawRect2D(ndcCx - halfW, ndcCy - halfH, halfW * 2.0f, halfH * 2.0f, rgba);
}

void emitRects(rhi::IRhi* r, float cx, float cy, float halfSize, float aspect,
               const LogicalRect* parts, int count, const float rgba[4]) {
	for (int i = 0; i < count; ++i) emitRect(r, cx, cy, halfSize, aspect, parts[i], rgba);
}

// ── Per-kind silhouettes ─────────────────────────────────────────────────
// Each is a hand-tuned set of rects in [-1, +1]² logical space. Kept
// small + chunky so they read at thumbnail size against any backdrop.

void drawAttackIcon(rhi::IRhi* r, float cx, float cy, float halfSize,
                    float aspect, const float rgba[4]) {
	// Sword pointing down: blade + crossguard + pommel.
	const LogicalRect parts[] = {
		// Pommel (top)
		{ 0.0f,  0.55f, 0.10f, 0.06f},
		// Crossguard (horizontal bar above the blade)
		{ 0.0f,  0.34f, 0.50f, 0.07f},
		// Blade (long vertical down to the tip)
		{ 0.0f, -0.10f, 0.10f, 0.45f},
	};
	emitRects(r, cx, cy, halfSize, aspect, parts, 3, rgba);
}

void drawHarvestIcon(rhi::IRhi* r, float cx, float cy, float halfSize,
                     float aspect, const float rgba[4]) {
	// Pickaxe: head across the top, handle hanging straight down.
	const LogicalRect parts[] = {
		// Head (top horizontal slab)
		{ 0.0f,  0.50f, 0.55f, 0.10f},
		// Head spike accents (pickaxe ends — small squares poking out)
		{-0.55f, 0.40f, 0.10f, 0.10f},
		{ 0.55f, 0.40f, 0.10f, 0.10f},
		// Handle (vertical down to the bottom)
		{ 0.0f, -0.10f, 0.10f, 0.45f},
	};
	emitRects(r, cx, cy, halfSize, aspect, parts, 4, rgba);
}

void drawInteractIcon(rhi::IRhi* r, float cx, float cy, float halfSize,
                      float aspect, const float rgba[4]) {
	// Ring + center dot — "press here" universal sign. Aspect-aware via
	// drawArc2D's own aspect arg so the ring stays round on widescreen.
	const float rOuter = halfSize * 0.55f;
	const float rInner = halfSize * 0.38f;
	const float dotR   = halfSize * 0.16f;
	r->drawArc2D(cx, cy, rInner, rOuter,
	             0.0f, 6.28318530718f, rgba, aspect, 36);
	r->drawArc2D(cx, cy, 0.0f, dotR,
	             0.0f, 6.28318530718f, rgba, aspect, 24);
}

void drawRelocateIcon(rhi::IRhi* r, float cx, float cy, float halfSize,
                      float aspect, const float rgba[4]) {
	// Chest: outlined box + lid line + small clasp dot.
	const LogicalRect parts[] = {
		// Outline frame (4 sides)
		{ 0.0f,  0.60f, 0.65f, 0.06f},  // top
		{ 0.0f, -0.60f, 0.65f, 0.06f},  // bottom
		{-0.59f, 0.0f,  0.06f, 0.60f},  // left
		{ 0.59f, 0.0f,  0.06f, 0.60f},  // right
		// Lid divider (horizontal at upper third)
		{ 0.0f,  0.18f, 0.55f, 0.05f},
		// Clasp (small square just under the lid line)
		{ 0.0f,  0.05f, 0.10f, 0.07f},
	};
	emitRects(r, cx, cy, halfSize, aspect, parts, 6, rgba);
}

void drawMoveIcon(rhi::IRhi* r, float cx, float cy, float halfSize,
                  float aspect, const float rgba[4]) {
	// Down-pointing arrow / chevron — "move to here". A vertical shaft
	// plus two diagonal-equivalent rects forming the arrowhead. We
	// approximate diagonals with stair-stepped horizontal slabs since
	// drawRect2D is axis-aligned only.
	const LogicalRect parts[] = {
		// Shaft (top half)
		{ 0.0f,  0.30f, 0.10f, 0.30f},
		// Arrowhead — three stacked horizontals tapering down
		{ 0.0f, -0.08f, 0.40f, 0.08f},
		{ 0.0f, -0.28f, 0.25f, 0.08f},
		{ 0.0f, -0.46f, 0.10f, 0.08f},
	};
	emitRects(r, cx, cy, halfSize, aspect, parts, 4, rgba);
}

} // namespace

const float* defaultIconColor(ActionIconKind kind) {
	// Genre conventions: red=hostile, green=interactable, yellow=harvest,
	// orange=container, white=neutral move target.
	static const float kRedAttack[4]    = {1.00f, 0.55f, 0.45f, 0.95f};
	static const float kYellowMine[4]   = {0.95f, 0.90f, 0.55f, 0.95f};
	static const float kGreenInter[4]   = {0.55f, 0.95f, 0.70f, 0.95f};
	static const float kOrangeReloc[4]  = {0.98f, 0.72f, 0.40f, 0.95f};
	static const float kWhiteMove[4]    = {0.92f, 0.92f, 0.96f, 0.85f};
	switch (kind) {
		case ActionIconKind::Attack:   return kRedAttack;
		case ActionIconKind::Harvest:  return kYellowMine;
		case ActionIconKind::Interact: return kGreenInter;
		case ActionIconKind::Relocate: return kOrangeReloc;
		case ActionIconKind::Move:     return kWhiteMove;
	}
	return kWhiteMove;
}

void drawActionIcon(rhi::IRhi* rhi, ActionIconKind kind,
                    float cx, float cy, float halfSize, float aspect,
                    const float rgba[4]) {
	if (!rhi || halfSize <= 0.0f) return;
	switch (kind) {
		case ActionIconKind::Attack:   drawAttackIcon  (rhi, cx, cy, halfSize, aspect, rgba); break;
		case ActionIconKind::Harvest:  drawHarvestIcon (rhi, cx, cy, halfSize, aspect, rgba); break;
		case ActionIconKind::Interact: drawInteractIcon(rhi, cx, cy, halfSize, aspect, rgba); break;
		case ActionIconKind::Relocate: drawRelocateIcon(rhi, cx, cy, halfSize, aspect, rgba); break;
		case ActionIconKind::Move:     drawMoveIcon    (rhi, cx, cy, halfSize, aspect, rgba); break;
	}
}

} // namespace civcraft::vk

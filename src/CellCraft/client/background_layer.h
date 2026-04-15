// CellCraft — ambient "looming creatures" background layer.
//
// Purely visual silhouettes that drift slowly in world-space behind the
// gameplay. No collision, no damage, no sim side effects. The set of
// silhouettes shown depends on the player's current growth Tier so the
// background always previews "the next scale up":
//
//   tier 1 (SPECK)    → big blurry CELLs (≈ tier-2 size)
//   tier 2 (NIBBLER)  → bigger CELLs
//   tier 3 (HUNTER)   → giant CELLs
//   tier 4 (PREDATOR) → huge CELLs approaching animal scale
//   tier 5 (APEX)     → fish / turtles / jellies — animal-world silhouettes
//
// Rendering: each silhouette is ONE single closed chalk-outline ribbon at a
// uniform stroke width, in warm tan. No stacked passes — stacking at different
// widths produced criss-cross scribbles that didn't read as recognizable
// shapes. Called between drawBoard() and drawFood()/drawMonsters().
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/cell_fill_renderer.h"
#include "CellCraft/client/chalk_stroke.h"

namespace civcraft::cellcraft {

enum class SilhouetteType : uint8_t {
	CELL_GENERIC = 0,
	FISH,
	TURTLE,
	JELLY,
};

struct BgCreature {
	glm::vec2      pos       = glm::vec2(0.0f);   // world space
	glm::vec2      vel       = glm::vec2(0.0f);
	float          scale     = 1.0f;              // relative to baseline silhouette size
	float          rot       = 0.0f;              // radians
	float          rot_vel   = 0.0f;
	int            layer     = 0;                 // 0=near, 1=mid, 2=far
	SilhouetteType type      = SilhouetteType::CELL_GENERIC;
	float          wobble_ph = 0.0f;              // phase for lobed-blob jiggle

	// Depth-of-field knobs. parallax = how much the camera move shifts this
	// creature: 1.0 = fully world-locked (near), 0.3 = mostly screen-locked
	// (far, looks distant). stroke_width = ribbon half-width; larger values
	// on far layers + board-cream tint mix fake a gaussian blur without
	// needing an FBO pass. tint_mix blends the stroke color toward the
	// board color (0 = full tint, 1 = invisible).
	float          parallax     = 1.0f;
	float          stroke_width = 4.0f;
	float          tint_mix     = 0.0f;
};

class BackgroundLayer {
public:
	void init(unsigned seed = 0xB6E71u);

	// Update positions + wrap around the play field. map_radius in world units.
	void update(float dt, float map_radius);

	// (Re)populate the creature pool to match player_tier. No-op if the tier
	// hasn't changed. Called each frame; cheap.
	void setPlayerTier(int player_tier, float map_radius);

	// Draw silhouettes. world_to_screen is a functor: glm::vec2 w -> glm::vec2 px.
	// cam_world is the camera center in world space — needed for parallax so
	// far-layer creatures move slower than near-layer ones when the camera
	// pans. Tinted toward `board_cream` (far layers blend further toward it)
	// so they recede visually.
	//
	// If fill is non-null, each creature also gets a body-fill pass using the
	// cell_body shader — same material as arena cells — with per-layer alpha
	// so far layers look atmospheric. Outlines draw on top of all fills.
	template<typename ToScreen>
	void draw(ChalkRenderer* r, int screen_w, int screen_h,
	          ToScreen world_to_screen, glm::vec2 cam_world, float time_seconds,
	          CellFillRenderer* fill = nullptr);

private:
	void repopulate_(int tier, float map_radius);
	BgCreature spawn_(SilhouetteType t, float map_radius, float scale, int layer);

	void buildCellStrokes_  (const BgCreature& c, std::vector<ChalkStroke>& out,
	                         glm::vec3 tint, float t_sec,
	                         float (*ws_x_cb)(void*, float, float),
	                         float (*ws_y_cb)(void*, float, float),
	                         void* ws_user) const;

	std::vector<BgCreature> pool_;
	std::mt19937            rng_;
	int                     current_tier_ = -1;

	// Reusable scratch buffer.
	mutable std::vector<ChalkStroke> scratch_;
};

// ---- Template impl -------------------------------------------------------
// Kept in-header so the templated world_to_screen callable inlines cleanly.

namespace bg_detail {

// One single closed outline ribbon. Thick single pass — NO stacking of
// multiple widths, which previously produced criss-cross scribbles rather
// than recognizable silhouettes.
inline void appendOutline(std::vector<ChalkStroke>& out,
                          const std::vector<glm::vec2>& pts_px,
                          glm::vec3 tint, bool closed,
                          float half_width = 4.0f) {
	if (pts_px.empty()) return;
	ChalkStroke s;
	s.half_width = half_width;
	s.color      = tint;
	s.points     = pts_px;
	if (closed && !s.points.empty()) s.points.push_back(s.points.front());
	out.push_back(std::move(s));
}

// Cheap fake-blur: emit the same outline N times at jittered radial offsets
// around the centroid, each with heavier feather (higher half_width) and the
// tint already biased toward cream. For near layers N=1 and this is a plain
// ribbon — the blur passes cost nothing.
inline void appendBlurredOutline(std::vector<ChalkStroke>& out,
                                 const std::vector<glm::vec2>& pts_px,
                                 glm::vec3 tint, bool closed,
                                 float half_width, int blur_passes,
                                 float blur_radius_px) {
	if (pts_px.empty()) return;
	appendOutline(out, pts_px, tint, closed, half_width);
	if (blur_passes <= 0) return;
	// Centroid — used to pick jitter directions that don't all bunch one way.
	glm::vec2 centroid(0.0f);
	for (const auto& p : pts_px) centroid += p;
	centroid /= float(pts_px.size());
	for (int k = 0; k < blur_passes; ++k) {
		float ang = 6.28318530718f * (float(k) + 0.5f) / float(blur_passes);
		glm::vec2 off(std::cos(ang) * blur_radius_px,
		              std::sin(ang) * blur_radius_px);
		std::vector<glm::vec2> shifted;
		shifted.reserve(pts_px.size());
		for (const auto& p : pts_px) shifted.push_back(p + off);
		appendOutline(out, shifted, tint, closed, half_width * 1.3f);
	}
}

inline void appendEllipseOutline(std::vector<ChalkStroke>& out,
                                 glm::vec2 center_px, float rx_px, float ry_px,
                                 float rot, glm::vec3 tint,
                                 int samples = 16,
                                 float half_width = 4.0f) {
	std::vector<glm::vec2> pts;
	pts.reserve(samples);
	float c = std::cos(rot), s = std::sin(rot);
	for (int i = 0; i < samples; ++i) {
		float ang = 6.28318530718f * float(i) / float(samples);
		float x = std::cos(ang) * rx_px;
		float y = std::sin(ang) * ry_px;
		pts.push_back(center_px + glm::vec2(c * x - s * y, s * x + c * y));
	}
	appendOutline(out, pts, tint, /*closed=*/true, half_width);
}

} // namespace bg_detail

template<typename ToScreen>
void BackgroundLayer::draw(ChalkRenderer* r, int screen_w, int screen_h,
                           ToScreen world_to_screen, glm::vec2 cam_world,
                           float t_sec, CellFillRenderer* fill) {
	if (!r || pool_.empty()) return;
	scratch_.clear();

	// Board cream is ~(0.97, 0.95, 0.88). Silhouettes drawn in warm tan;
	// far layers blend toward cream per-creature via c.tint_mix, so the
	// "layered world" visibly recedes with depth.
	const glm::vec3 TINT_BASE = glm::vec3(0.45f, 0.38f, 0.28f);
	const glm::vec3 BOARD_CREAM = glm::vec3(0.97f, 0.95f, 0.88f);
	// Fill base palettes — warmer than the outline tint so fills read as
	// muted organic bodies rather than pure shadow. Per-layer alpha fakes
	// atmospheric perspective.
	const glm::vec3 FILL_BASE   = glm::vec3(0.78f, 0.66f, 0.48f);   // warm tan
	const float     FILL_ALPHA[3] = { 0.55f, 0.32f, 0.18f };        // near→far

	for (const auto& c : pool_) {
		// Parallax: effective world pos lerps between camera center (far)
		// and true world pos (near). p=1 → full parallax, p=0 → sticks to
		// screen. Far layers use p≈0.3 so panning reveals them slowly.
		glm::vec2 effective_pos = cam_world + (c.pos - cam_world) * c.parallax;
		glm::vec3 tint = glm::mix(TINT_BASE, BOARD_CREAM,
		                          glm::clamp(c.tint_mix, 0.0f, 0.95f));
		float hw = c.stroke_width;
		int   blur_n = (c.layer >= 2) ? 4 : (c.layer == 1 ? 2 : 0);
		float blur_r = c.stroke_width * 0.9f;
		glm::vec2 center = world_to_screen(effective_pos);

		float cr = std::cos(c.rot), sr = std::sin(c.rot);
		auto rot_add = [&](glm::vec2 v){
			return center + glm::vec2(cr*v.x - sr*v.y, sr*v.x + cr*v.y);
		};

		// Per-creature fill helper: tint blends toward cream with tint_mix so
		// the fill recedes the same way the outline does. Uses diet_mix=0 so
		// no red/green/purple tint leaks into background scenery.
		int layer_idx = glm::clamp(c.layer, 0, 2);
		float fill_alpha = FILL_ALPHA[layer_idx];
		glm::vec3 fill_base = glm::mix(FILL_BASE, BOARD_CREAM,
			glm::clamp(c.tint_mix * 0.6f, 0.0f, 0.9f));
		float fill_seed = c.wobble_ph * 0.27f + float(layer_idx) * 1.3f;
		auto do_fill = [&](const std::vector<glm::vec2>& poly) {
			if (!fill) return;
			fill->drawFill(poly, fill_base, sim::Diet::OMNIVORE,
			               fill_seed, t_sec, screen_w, screen_h,
			               /*diet_mix=*/0.0f, fill_alpha);
		};

		switch (c.type) {
		case SilhouetteType::CELL_GENERIC: {
			// Lobed blob: closed polyline with sinusoidal radius jiggle.
			constexpr int N = 20;
			std::vector<glm::vec2> pts;
			pts.reserve(N);
			float base_r = 28.0f * c.scale;
			for (int i = 0; i < N; ++i) {
				float a = 6.28318530718f * float(i) / float(N);
				float lobe = 0.85f + 0.30f * std::sin(a * 3.0f + c.wobble_ph + t_sec * 0.6f);
				float rr = base_r * lobe;
				glm::vec2 p_local(std::cos(a + c.rot) * rr,
				                  std::sin(a + c.rot) * rr);
				pts.push_back(center + p_local);
			}
			do_fill(pts);
			bg_detail::appendBlurredOutline(scratch_, pts, tint, /*closed=*/true,
				hw, blur_n, blur_r);
			break;
		}
		case SilhouetteType::FISH: {
			// Single closed outline: teardrop body (wide front, tapered rear)
			// + triangular tail sticking out behind. One continuous ribbon.
			float rx = 40.0f * c.scale;
			float ry = 18.0f * c.scale;
			std::vector<glm::vec2> pts;
			pts.reserve(10);
			// Walk the silhouette clockwise starting at the nose (+x).
			pts.push_back(rot_add({ rx * 1.05f,  0.0f}));           // nose
			pts.push_back(rot_add({ rx * 0.55f,  ry * 0.95f}));     // upper front
			pts.push_back(rot_add({-rx * 0.10f,  ry * 1.00f}));     // back hump
			pts.push_back(rot_add({-rx * 0.70f,  ry * 0.55f}));     // pre-tail upper
			pts.push_back(rot_add({-rx * 1.60f,  ry * 1.10f}));     // tail upper tip
			pts.push_back(rot_add({-rx * 1.15f,  0.0f}));           // tail notch
			pts.push_back(rot_add({-rx * 1.60f, -ry * 1.10f}));     // tail lower tip
			pts.push_back(rot_add({-rx * 0.70f, -ry * 0.55f}));     // pre-tail lower
			pts.push_back(rot_add({-rx * 0.10f, -ry * 1.00f}));     // belly
			pts.push_back(rot_add({ rx * 0.55f, -ry * 0.95f}));     // lower front
			do_fill(pts);
			bg_detail::appendBlurredOutline(scratch_, pts, tint, /*closed=*/true,
				hw, blur_n, blur_r);
			break;
		}
		case SilhouetteType::TURTLE: {
			// Oval shell (12 samples) + small head disc forward + 4 leg bumps.
			float sx = 38.0f * c.scale;
			float sy = 26.0f * c.scale;
			// Fill just the shell (the dominant body mass).
			if (fill) {
				std::vector<glm::vec2> shell;
				shell.reserve(12);
				float cc = std::cos(c.rot), ss = std::sin(c.rot);
				for (int i = 0; i < 12; ++i) {
					float a = 6.28318530718f * float(i) / 12.0f;
					float lx = std::cos(a) * sx, ly = std::sin(a) * sy;
					shell.push_back(center + glm::vec2(cc*lx - ss*ly, ss*lx + cc*ly));
				}
				do_fill(shell);
			}
			bg_detail::appendEllipseOutline(scratch_, center, sx, sy, c.rot,
				tint, /*samples=*/12, hw);
			// Head: small disc forward of shell.
			float hr = 9.0f * c.scale;
			glm::vec2 head = rot_add({sx * 1.05f, 0.0f});
			bg_detail::appendEllipseOutline(scratch_, head, hr, hr, 0.0f,
				tint, /*samples=*/10, hw);
			// 4 leg bumps on shell perimeter (diagonal).
			float lr = 7.0f * c.scale;
			glm::vec2 leg_off[4] = {
				{ sx * 0.70f,  sy * 0.85f},
				{-sx * 0.70f,  sy * 0.85f},
				{ sx * 0.70f, -sy * 0.85f},
				{-sx * 0.70f, -sy * 0.85f},
			};
			for (int i = 0; i < 4; ++i) {
				glm::vec2 lp = rot_add(leg_off[i]);
				bg_detail::appendEllipseOutline(scratch_, lp, lr, lr * 0.75f,
					c.rot, tint, /*samples=*/8, hw);
			}
			break;
		}
		case SilhouetteType::JELLY: {
			// Dome outline (half-ellipse top) with a wavy bottom edge, closed
			// as a single ribbon. Plus 3 long trailing tentacle lines.
			float dx = 32.0f * c.scale;
			float dy = 22.0f * c.scale;
			std::vector<glm::vec2> pts;
			// Top half: from right rim, over the top, to the left rim.
			constexpr int TOP_N = 10;
			for (int i = 0; i <= TOP_N; ++i) {
				float a = float(i) / float(TOP_N) * 3.14159265f;  // 0..pi
				glm::vec2 p(std::cos(a) * dx, -std::sin(a) * dy);
				pts.push_back(rot_add(p));
			}
			// Wavy bottom edge: from left rim back to right rim with gentle undulation.
			constexpr int BOT_N = 8;
			for (int i = 1; i < BOT_N; ++i) {
				float t = float(i) / float(BOT_N);  // 0..1
				float lx = -dx + 2.0f * dx * t;
				float wave = std::sin(t * 6.28318f + c.wobble_ph + t_sec * 1.2f) * (dy * 0.18f);
				pts.push_back(rot_add({lx, wave}));
			}
			do_fill(pts);
			bg_detail::appendBlurredOutline(scratch_, pts, tint, /*closed=*/true,
				hw, blur_n, blur_r);
			// 3 trailing tentacles curving downward.
			for (int i = 0; i < 3; ++i) {
				float ox = (-1.0f + float(i)) * dx * 0.55f;
				std::vector<glm::vec2> tent;
				tent.reserve(8);
				for (int k = 0; k < 8; ++k) {
					float yy = float(k) * dy * 0.55f;
					float wx = std::sin(t_sec * 1.3f + c.wobble_ph + k * 0.55f + i * 1.1f) * (dx * 0.12f);
					tent.push_back(rot_add({ox + wx, yy + dy * 0.05f}));
				}
				bg_detail::appendOutline(scratch_, tent, tint, /*closed=*/false,
					std::max(3.0f, hw * 0.75f));
			}
			break;
		}
		}
	}

	r->drawStrokes(scratch_, nullptr, screen_w, screen_h);
}

} // namespace civcraft::cellcraft

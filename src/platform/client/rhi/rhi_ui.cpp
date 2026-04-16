// Shared CPU tessellation for drawText2D / drawRect2D / drawArc2D. Lives
// on the IRhi base so both backends compile the same helper — each backend
// only needs to implement the drawUi2D primitive (upload a triangle list
// of {pos.xy, uv.xy} vertices and draw them with the UI pipeline).

#include "rhi.h"
#include "ui_font_8x8.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace civcraft::rhi {

// Glyph size in NDC. Tuned to match the original GL TextRenderer so the
// migration preserves visual layout exactly. 1.0 scale ≈ 24 px on 900p.
static constexpr float kCharW = 0.018f;
static constexpr float kCharH = 0.032f;

static void appendGlyph(std::vector<float>& verts, char c,
                        float cx, float y, float w, float h) {
	int idx = static_cast<int>(c) - 32;
	if (idx < 0 || idx >= 96) idx = 0;
	float u0 = (idx % kFontGridCols) / static_cast<float>(kFontGridCols);
	float v0 = (idx / kFontGridCols) / static_cast<float>(kFontGridRows);
	float u1 = u0 + 1.0f / kFontGridCols;
	float v1 = v0 + 1.0f / kFontGridRows;

	// Two tris per glyph, CCW in +y-up NDC.
	const float vd[] = {
		cx,     y,     u0, v1,
		cx + w, y,     u1, v1,
		cx + w, y + h, u1, v0,
		cx,     y,     u0, v1,
		cx + w, y + h, u1, v0,
		cx,     y + h, u0, v0,
	};
	verts.insert(verts.end(), std::begin(vd), std::end(vd));
}

static void buildTextVerts(std::vector<float>& verts, const char* text,
                           float x, float y, float scale) {
	float charW = scale * kCharW;
	float charH = scale * kCharH;
	float cx = x;
	for (const char* p = text; *p; p++) {
		appendGlyph(verts, *p, cx, y, charW, charH);
		cx += charW;
	}
}

void IRhi::drawText2D(const char* text, float x, float y, float scale,
                      const float rgba[4]) {
	if (!text || !*text) return;
	std::vector<float> verts;
	buildTextVerts(verts, text, x, y, scale);
	if (verts.empty()) return;
	drawUi2D(verts.data(), static_cast<uint32_t>(verts.size() / 4),
	         /*mode=*/0, rgba);
}

void IRhi::drawTitle2D(const char* text, float x, float y, float scale,
                       const float rgba[4]) {
	if (!text || !*text) return;
	std::vector<float> verts;
	buildTextVerts(verts, text, x, y, scale);
	if (verts.empty()) return;
	drawUi2D(verts.data(), static_cast<uint32_t>(verts.size() / 4),
	         /*mode=*/2, rgba);
}

void IRhi::drawRect2D(float x, float y, float w, float h,
                      const float rgba[4]) {
	const float verts[] = {
		x,     y,     0.0f, 0.0f,
		x + w, y,     1.0f, 0.0f,
		x + w, y + h, 1.0f, 1.0f,
		x,     y,     0.0f, 0.0f,
		x + w, y + h, 1.0f, 1.0f,
		x,     y + h, 0.0f, 1.0f,
	};
	drawUi2D(verts, 6, /*mode=*/1, rgba);
}

void IRhi::drawArc2D(float cx, float cy, float r_inner, float r_outer,
                     float startRad, float endRad, const float rgba[4],
                     float aspect, int segments) {
	if (segments < 1 || r_outer <= r_inner) return;
	float sweep = endRad - startRad;
	if (sweep == 0.0f) return;

	std::vector<float> verts;
	verts.reserve(segments * 6 * 4);
	for (int i = 0; i < segments; i++) {
		float a0 = startRad + sweep * static_cast<float>(i) / segments;
		float a1 = startRad + sweep * static_cast<float>(i + 1) / segments;
		float c0 = std::cos(a0), s0 = std::sin(a0);
		float c1 = std::cos(a1), s1 = std::sin(a1);
		// x in NDC-x directly; y scaled by aspect so circles stay round.
		float ox0 = cx + r_outer * c0, oy0 = cy + r_outer * aspect * s0;
		float ox1 = cx + r_outer * c1, oy1 = cy + r_outer * aspect * s1;
		float ix0 = cx + r_inner * c0, iy0 = cy + r_inner * aspect * s0;
		float ix1 = cx + r_inner * c1, iy1 = cy + r_inner * aspect * s1;
		const float seg[] = {
			ox0, oy0, 0, 0,  ix0, iy0, 0, 0,  ox1, oy1, 0, 0,
			ix0, iy0, 0, 0,  ix1, iy1, 0, 0,  ox1, oy1, 0, 0,
		};
		verts.insert(verts.end(), std::begin(seg), std::end(seg));
	}
	drawUi2D(verts.data(), static_cast<uint32_t>(verts.size() / 4),
	         /*mode=*/1, rgba);
}

} // namespace civcraft::rhi

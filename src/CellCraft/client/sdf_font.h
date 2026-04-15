#pragma once

// CellCraft SDF-ish TTF text renderer with arcade-neon glow support.
//
// Uses stb_truetype to bake a high-res alpha atlas from a TTF at init time.
// Because the atlas is baked at a large pixel size (default 64 px) with
// oversampling, sampling at smaller target sizes with GL_LINEAR gives
// smooth glyph edges — not true multi-channel SDF, but the soft alpha
// ramp is enough to produce a neon glow via a multi-tap shader blur.
//
// CellCraft-only: platform TextRenderer (8x8 bitmap) is left intact for
// CivCraft and for the chalk in-canvas title renderer.

#include "client/gl.h"
#include "client/shader.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace civcraft::cellcraft {

class SdfFont {
public:
	// Initialize from a TTF file. atlas_px is the bake pixel height (64 = good
	// for up to ~96 px on-screen). Returns false on failure.
	bool init(const std::string& ttfPath,
	          const std::string& shaderDir,
	          float atlas_px = 64.0f);
	void shutdown();

	bool ready() const { return m_ready; }

	// Metric for a given target pixel size.
	float measureWidth(const std::string& s,
	                   float pxSize,
	                   float tracking_px = 0.0f) const;
	float lineHeight(float pxSize) const;
	// Approximate ascent in pixels at pxSize (useful for converting
	// top-of-cap y → baseline y).
	float ascent(float pxSize) const;

	// Draw text with neon-glow capability. Pixel-space (origin top-left,
	// y-down). `baselineY` is the text baseline. For top-y callers, add
	// `ascent(pxSize)` to y.
	// - fill:          inner glyph color (alpha used for opacity)
	// - glow:          outer halo color (set a=0 to disable glow)
	// - glow_radius_px: halo radius in screen px (0 = no glow)
	// - glow_intensity: 0..2 multiplier on halo alpha
	void draw(const std::string& s,
	          float baselineX, float baselineY,
	          float pxSize,
	          const glm::vec4& fill,
	          const glm::vec4& glow = glm::vec4(0.0f),
	          float glow_radius_px = 0.0f,
	          float glow_intensity = 1.0f,
	          float tracking_px = 0.0f,
	          int screen_w_px = 1280, int screen_h_px = 800);

private:
	struct BakedChar {
		// Atlas-relative uv box, and glyph pixel metrics relative to pen.
		float u0, v0, u1, v1;
		float xoff, yoff;
		float xoff2, yoff2;   // bottom-right offsets
		float xadvance;
	};

	bool   m_ready     = false;
	float  m_atlasPx   = 64.0f;
	float  m_ascent_px = 0.0f;   // at m_atlasPx
	float  m_descent_px = 0.0f;
	float  m_lineGap_px = 0.0f;
	int    m_atlasW = 1024;
	int    m_atlasH = 1024;
	int    m_pad_px = 4;          // bake-time inter-glyph padding, atlas texels
	GLuint m_tex    = 0;
	GLuint m_vao    = 0;
	GLuint m_vbo    = 0;
	Shader m_shader;
	std::vector<BakedChar> m_chars;   // ASCII 32..126 (95 entries)

	void uploadDraw(const float* verts, size_t floatCount,
	                const glm::vec4& fill, const glm::vec4& glow,
	                float glow_radius_uv, float glow_intensity,
	                int screen_w_px, int screen_h_px);
};

} // namespace civcraft::cellcraft

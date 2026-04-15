// SdfFont — see sdf_font.h. Uses stb_truetype pack API.

#include "CellCraft/client/sdf_font.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include "CellCraft/vendor/stb_truetype.h"

namespace civcraft::cellcraft {

static constexpr int FIRST_CHAR = 32;
static constexpr int NUM_CHARS  = 95; // 32..126

bool SdfFont::init(const std::string& ttfPath,
                   const std::string& shaderDir,
                   float atlas_px) {
	// Load file.
	FILE* f = std::fopen(ttfPath.c_str(), "rb");
	if (!f) {
		std::fprintf(stderr, "[sdf_font] cannot open %s\n", ttfPath.c_str());
		return false;
	}
	std::fseek(f, 0, SEEK_END);
	long sz = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<unsigned char> buf(sz);
	if (std::fread(buf.data(), 1, sz, f) != (size_t)sz) { std::fclose(f); return false; }
	std::fclose(f);

	m_atlasPx = atlas_px;
	// Large-display fonts (e.g. Audiowide at 96px) need a bigger atlas
	// because each glyph consumes ~96² texels even before oversampling.
	m_atlasW = (atlas_px >= 80.0f) ? 2048 : 1024;
	m_atlasH = m_atlasW;

	// Pack glyphs with oversampling for crisper sub-pixel rendering.
	std::vector<unsigned char> pixels(m_atlasW * m_atlasH, 0);
	stbtt_pack_context pc;
	// Padding between glyphs so the 9-tap gaussian glow doesn't leak into
	// neighbors. Display-size fonts need more padding because glow radius
	// scales with target px.
	int pad = (atlas_px >= 80.0f) ? 16 : 4;
	m_pad_px = pad;
	if (!stbtt_PackBegin(&pc, pixels.data(), m_atlasW, m_atlasH, 0, pad, nullptr)) {
		std::fprintf(stderr, "[sdf_font] PackBegin failed\n");
		return false;
	}
	// Oversampling helps small text but wastes atlas space on display fonts.
	if (atlas_px < 80.0f) stbtt_PackSetOversampling(&pc, 2, 2);

	std::vector<stbtt_packedchar> chardata(NUM_CHARS);
	int packResult = stbtt_PackFontRange(&pc, buf.data(), 0, atlas_px,
	                         FIRST_CHAR, NUM_CHARS, chardata.data());
	if (!packResult) {
		std::fprintf(stderr, "[sdf_font] PackFontRange failed for %s at %.0fpx\n",
			ttfPath.c_str(), atlas_px);
		stbtt_PackEnd(&pc);
		return false;
	}
	stbtt_PackEnd(&pc);

	// Font vmetrics at atlas_px scale.
	stbtt_fontinfo info;
	if (!stbtt_InitFont(&info, buf.data(), stbtt_GetFontOffsetForIndex(buf.data(), 0))) {
		return false;
	}
	float scale = stbtt_ScaleForPixelHeight(&info, atlas_px);
	int ascent_i, descent_i, lineGap_i;
	stbtt_GetFontVMetrics(&info, &ascent_i, &descent_i, &lineGap_i);
	m_ascent_px   = ascent_i  * scale;
	m_descent_px  = descent_i * scale;
	m_lineGap_px  = lineGap_i * scale;

	// Stash baked chars (stb packedchar -> our struct).
	m_chars.resize(NUM_CHARS);
	for (int i = 0; i < NUM_CHARS; ++i) {
		const auto& c = chardata[i];
		BakedChar b{};
		b.u0 = c.x0 / (float)m_atlasW;
		b.v0 = c.y0 / (float)m_atlasH;
		b.u1 = c.x1 / (float)m_atlasW;
		b.v1 = c.y1 / (float)m_atlasH;
		b.xoff = c.xoff;   b.yoff  = c.yoff;
		b.xoff2 = c.xoff2; b.yoff2 = c.yoff2;
		b.xadvance = c.xadvance;
		m_chars[i] = b;
	}

	// Upload atlas.
	glGenTextures(1, &m_tex);
	glBindTexture(GL_TEXTURE_2D, m_tex);
#ifdef __EMSCRIPTEN__
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasW, m_atlasH, 0,
		GL_RED, GL_UNSIGNED_BYTE, pixels.data());
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_atlasW, m_atlasH, 0,
		GL_RED, GL_UNSIGNED_BYTE, pixels.data());
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Shader.
	if (!m_shader.loadFromFile(shaderDir + "/text_modern.vert",
	                           shaderDir + "/text_modern.frag")) {
		std::fprintf(stderr, "[sdf_font] shader load failed\n");
		return false;
	}

	// VAO/VBO (pos.xy + uv.xy per vertex).
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	// Vertex layout: pos.xy (8B) + uv.xy (8B) + uv_box.xyzw (16B) = 32B/vert.
	glBufferData(GL_ARRAY_BUFFER, 256 * 1024, nullptr, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glBindVertexArray(0);

	m_ready = true;
	return true;
}

void SdfFont::shutdown() {
	if (m_tex) glDeleteTextures(1, &m_tex);
	if (m_vbo) glDeleteBuffers(1, &m_vbo);
	if (m_vao) glDeleteVertexArrays(1, &m_vao);
	m_tex = m_vbo = m_vao = 0;
	m_ready = false;
}

float SdfFont::measureWidth(const std::string& s, float pxSize, float tracking_px) const {
	if (!m_ready || s.empty()) return 0.0f;
	float s_scale = pxSize / m_atlasPx;
	float x = 0.0f;
	for (size_t i = 0; i < s.size(); ++i) {
		int cp = (unsigned char)s[i];
		if (cp < FIRST_CHAR || cp >= FIRST_CHAR + NUM_CHARS) cp = '?';
		const auto& b = m_chars[cp - FIRST_CHAR];
		x += b.xadvance * s_scale;
		if (i + 1 < s.size()) x += tracking_px;
	}
	return x;
}

float SdfFont::lineHeight(float pxSize) const {
	float s = pxSize / m_atlasPx;
	return (m_ascent_px - m_descent_px + m_lineGap_px) * s;
}

float SdfFont::ascent(float pxSize) const {
	return m_ascent_px * (pxSize / m_atlasPx);
}

void SdfFont::draw(const std::string& s,
                   float baselineX, float baselineY,
                   float pxSize,
                   const glm::vec4& fill,
                   const glm::vec4& glow,
                   float glow_radius_px,
                   float glow_intensity,
                   float tracking_px,
                   int screen_w_px, int screen_h_px) {
	if (!m_ready || s.empty() || screen_w_px <= 0 || screen_h_px <= 0) return;
	float s_scale = pxSize / m_atlasPx;

	// Build quads. Pixel-space (y-down). Convert to NDC in shader via
	// u_screen_size. Quads are expanded outward by the atlas padding (in
	// screen pixels) so the fragment shader has room to paint the gaussian
	// glow bloom beyond the glyph's tight edges. The tight glyph uv box is
	// passed as a per-vertex attribute so the shader can return 0 when a
	// blur sample lands outside the glyph cell — this kills neighbor-glyph
	// bleed that used to slice the hero text at DISPLAY size.
	//
	// Per-vertex layout: pos.xy, uv.xy, uv_box.xyzw = 8 floats.
	std::vector<float> verts;
	verts.reserve(s.size() * 6 * 8);
	float x = baselineX;
	float y = baselineY;
	float pad_px   = (float)m_pad_px * s_scale;           // quad outset, screen px
	float pad_u    = (float)m_pad_px / (float)m_atlasW;   // uv outset
	for (size_t i = 0; i < s.size(); ++i) {
		int cp = (unsigned char)s[i];
		if (cp < FIRST_CHAR || cp >= FIRST_CHAR + NUM_CHARS) cp = '?';
		const auto& b = m_chars[cp - FIRST_CHAR];
		float x0 = x + b.xoff  * s_scale - pad_px;
		float y0 = y + b.yoff  * s_scale - pad_px;
		float x1 = x + b.xoff2 * s_scale + pad_px;
		float y1 = y + b.yoff2 * s_scale + pad_px;
		float u0 = b.u0 - pad_u;
		float v0 = b.v0 - pad_u;
		float u1 = b.u1 + pad_u;
		float v1 = b.v1 + pad_u;
		float bx0 = b.u0, by0 = b.v0, bx1 = b.u1, by1 = b.v1; // tight box
		float quad[] = {
			x0, y0, u0, v0, bx0, by0, bx1, by1,
			x1, y0, u1, v0, bx0, by0, bx1, by1,
			x1, y1, u1, v1, bx0, by0, bx1, by1,
			x0, y0, u0, v0, bx0, by0, bx1, by1,
			x1, y1, u1, v1, bx0, by0, bx1, by1,
			x0, y1, u0, v1, bx0, by0, bx1, by1,
		};
		verts.insert(verts.end(), std::begin(quad), std::end(quad));
		x += b.xadvance * s_scale;
		if (i + 1 < s.size()) x += tracking_px;
	}
	if (verts.empty()) return;

	// Convert pixel glow_radius to uv radius (relative to atlas). No clamp
	// needed — the fragment shader rejects samples outside the tight glyph
	// uv box, so the gaussian cannot leak into neighbor glyphs even for
	// very large radii.
	float glow_uv = 0.0f;
	if (glow_radius_px > 0.0f && glow.a > 0.0f) {
		glow_uv = (glow_radius_px / s_scale) / (float)m_atlasW;
	}
	uploadDraw(verts.data(), verts.size(), fill, glow, glow_uv, glow_intensity,
	           screen_w_px, screen_h_px);
}

void SdfFont::uploadDraw(const float* verts, size_t floatCount,
                         const glm::vec4& fill, const glm::vec4& glow,
                         float glow_radius_uv, float glow_intensity,
                         int screen_w_px, int screen_h_px) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_shader.use();
	m_shader.setVec4("u_fill",  fill);
	m_shader.setVec4("u_glow",  glow);
	m_shader.setFloat("u_glow_radius", glow_radius_uv);
	m_shader.setFloat("u_glow_intensity", glow_intensity);
	m_shader.setVec2("u_screen_size", glm::vec2((float)screen_w_px, (float)screen_h_px));
	m_shader.setInt("u_atlas", 0);
	m_shader.setFloat("u_atlas_texel", 1.0f / (float)m_atlasW);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_tex);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(floatCount * sizeof(float)),
	             verts, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (int)(floatCount / 8));

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

} // namespace civcraft::cellcraft

#include "client/text.h"
#include "client/rhi/ui_font_8x8.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace civcraft {

// The 8×8 glyph data and SDF atlas generator live in rhi/ui_font_8x8.h so
// the GL TextRenderer and the Vulkan RHI share a single source of truth
// (the SDF is byte-identical across backends).

void TextRenderer::generateFontTexture() {
	std::vector<uint8_t> sdf;
	rhi::generateUiFontAtlas(sdf);

	glGenTextures(1, &m_fontTexture);
	glBindTexture(GL_TEXTURE_2D, m_fontTexture);
#ifdef __EMSCRIPTEN__
	// WebGL 2 requires sized internal format GL_R8
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
		rhi::kFontAtlasW, rhi::kFontAtlasH, 0,
		GL_RED, GL_UNSIGNED_BYTE, sdf.data());
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
		rhi::kFontAtlasW, rhi::kFontAtlasH, 0,
		GL_RED, GL_UNSIGNED_BYTE, sdf.data());
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool TextRenderer::init(const std::string& shaderDir) {
	if (!m_textShader.loadFromFile(shaderDir + "/text.vert", shaderDir + "/text.frag"))
		return false;

	generateFontTexture();

	// Dynamic VBO for text quads
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	// pos (2) + uv (2) = 4 floats per vertex
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
	return true;
}

void TextRenderer::shutdown() {
	if (m_fontTexture) glDeleteTextures(1, &m_fontTexture);
	if (m_vbo) glDeleteBuffers(1, &m_vbo);
	if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

static void buildTextVerts(std::vector<float>& verts, const std::string& text,
                           float x, float y, float scale) {
	float charW = scale * 0.018f;
	float charH = scale * 0.032f;

	float cx = x;
	for (char c : text) {
		int idx = c - 32;
		if (idx < 0 || idx >= 96) idx = 0;
		float u0 = (idx % 16) / 16.0f;
		float v0 = (idx / 16) / 6.0f;
		float u1 = u0 + 1.0f / 16.0f;
		float v1 = v0 + 1.0f / 6.0f;

		float vd[] = {
			cx,        y,        u0, v1,
			cx+charW,  y,        u1, v1,
			cx+charW,  y+charH,  u1, v0,
			cx,        y,        u0, v1,
			cx+charW,  y+charH,  u1, v0,
			cx,        y+charH,  u0, v0,
		};
		verts.insert(verts.end(), std::begin(vd), std::end(vd));
		cx += charW;
	}
}

void TextRenderer::uploadAndDraw(const float* verts, size_t floatCount,
                                 int mode, glm::vec4 color) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_textShader.use();
	m_textShader.setVec3("uColor", glm::vec3(color));
	m_textShader.setFloat("uAlpha", color.a);
	m_textShader.setInt("uFontTex", 0);
	m_textShader.setInt("uMode", mode);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_fontTexture);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(floatCount * sizeof(float)),
		verts, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (int)(floatCount / 4));

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void TextRenderer::drawText(const std::string& text, float x, float y,
                            float scale, glm::vec4 color, float /*aspect*/) {
	std::vector<float> verts;
	buildTextVerts(verts, text, x, y, scale);
	if (verts.empty()) return;
	uploadAndDraw(verts.data(), verts.size(), 0, color);
}

void TextRenderer::drawTitle(const std::string& text, float x, float y,
                             float scale, glm::vec4 color, float /*aspect*/) {
	std::vector<float> verts;
	buildTextVerts(verts, text, x, y, scale);
	if (verts.empty()) return;
	uploadAndDraw(verts.data(), verts.size(), 2, color);
}

void TextRenderer::drawRect(float x, float y, float w, float h, glm::vec4 color) {
	float verts[] = {
		x,   y,   0, 0,
		x+w, y,   1, 0,
		x+w, y+h, 1, 1,
		x,   y,   0, 0,
		x+w, y+h, 1, 1,
		x,   y+h, 0, 1,
	};
	uploadAndDraw(verts, std::size(verts), 1, color);
}

void TextRenderer::drawArc(float cx, float cy, float r_inner, float r_outer,
                           float startAngle, float endAngle,
                           glm::vec4 color, float aspect, int segments) {
	if (segments < 1 || r_outer <= r_inner) return;
	float sweep = endAngle - startAngle;
	if (sweep == 0.0f) return;

	// Each segment = 2 triangles. With r_inner=0, triangle 2 is degenerate (fine).
	std::vector<float> verts;
	verts.reserve(segments * 6 * 4);
	for (int i = 0; i < segments; i++) {
		float a0 = startAngle + sweep * (float)i / segments;
		float a1 = startAngle + sweep * (float)(i + 1) / segments;
		float c0 = std::cos(a0), s0 = std::sin(a0);
		float c1 = std::cos(a1), s1 = std::sin(a1);
		// x uses r directly; y scaled by aspect so circle is round on screen
		float ox0 = cx + r_outer * c0, oy0 = cy + r_outer * aspect * s0;
		float ox1 = cx + r_outer * c1, oy1 = cy + r_outer * aspect * s1;
		float ix0 = cx + r_inner * c0, iy0 = cy + r_inner * aspect * s0;
		float ix1 = cx + r_inner * c1, iy1 = cy + r_inner * aspect * s1;
		float seg[] = {
			ox0, oy0, 0, 0,  ix0, iy0, 0, 0,  ox1, oy1, 0, 0,
			ix0, iy0, 0, 0,  ix1, iy1, 0, 0,  ox1, oy1, 0, 0,
		};
		verts.insert(verts.end(), std::begin(seg), std::end(seg));
	}
	uploadAndDraw(verts.data(), verts.size(), 1, color);
}

} // namespace civcraft

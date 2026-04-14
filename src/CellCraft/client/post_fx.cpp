#include "CellCraft/client/post_fx.h"

#include <cstdio>

namespace civcraft::cellcraft {

bool PostFX::init() {
	if (!m_extract.loadFromFile("shaders/postfx.vert", "shaders/bloom_extract.frag")) return false;
	if (!m_blur.loadFromFile("shaders/postfx.vert", "shaders/bloom_blur.frag")) return false;
	if (!m_composite.loadFromFile("shaders/postfx.vert", "shaders/bloom_composite.frag")) return false;
	glGenVertexArrays(1, &m_vao);
	return true;
}

void PostFX::shutdown() {
	destroy_buffers_();
	if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
}

void PostFX::destroy_buffers_() {
	if (m_scene_fbo) { glDeleteFramebuffers(1, &m_scene_fbo); m_scene_fbo = 0; }
	if (m_scene_tex) { glDeleteTextures(1, &m_scene_tex); m_scene_tex = 0; }
	for (int i = 0; i < 2; ++i) {
		if (m_bloom_fbo[i]) { glDeleteFramebuffers(1, &m_bloom_fbo[i]); m_bloom_fbo[i] = 0; }
		if (m_bloom_tex[i]) { glDeleteTextures(1, &m_bloom_tex[i]); m_bloom_tex[i] = 0; }
	}
	m_w = m_h = m_bw = m_bh = 0;
}

static GLuint make_color_tex(int w, int h) {
	GLuint t; glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

static GLuint make_fbo(GLuint tex) {
	GLuint f; glGenFramebuffers(1, &f);
	glBindFramebuffer(GL_FRAMEBUFFER, f);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		std::fprintf(stderr, "PostFX: FBO incomplete (0x%x)\n", status);
	}
	return f;
}

void PostFX::ensure_size_(int w, int h) {
	if (w == m_w && h == m_h && m_scene_fbo) return;
	destroy_buffers_();
	m_w = w; m_h = h;
	m_bw = w / 2; m_bh = h / 2;
	if (m_bw < 1) m_bw = 1;
	if (m_bh < 1) m_bh = 1;

	m_scene_tex = make_color_tex(m_w, m_h);
	m_scene_fbo = make_fbo(m_scene_tex);

	for (int i = 0; i < 2; ++i) {
		m_bloom_tex[i] = make_color_tex(m_bw, m_bh);
		m_bloom_fbo[i] = make_fbo(m_bloom_tex[i]);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostFX::begin(int w, int h) {
	ensure_size_(w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo);
	glViewport(0, 0, m_w, m_h);
	// Cleared by the board shader anyway, but clear for safety.
	glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void PostFX::render_to_default(int w, int h, float time_seconds) {
	if (w != m_w || h != m_h) return; // caller must call begin() first

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(m_vao);

	// 1) Bright-pass extract → bloom_fbo[0] (half-res)
	glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[0]);
	glViewport(0, 0, m_bw, m_bh);
	m_extract.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_scene_tex);
	m_extract.setInt("u_scene", 0);
	m_extract.setFloat("u_threshold", bloom_threshold);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// 2) Separable Gaussian — 2 passes (H, V), then one more pair for softer glow.
	m_blur.use();
	m_blur.setInt("u_tex", 0);
	glm::vec2 texel(1.0f / (float)m_bw, 1.0f / (float)m_bh);
	for (int pass = 0; pass < 2; ++pass) {
		// Horizontal: bloom_tex[0] → bloom_fbo[1]
		glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[1]);
		glBindTexture(GL_TEXTURE_2D, m_bloom_tex[0]);
		m_blur.setVec2("u_texel", texel);
		m_blur.setVec2("u_dir", glm::vec2(1.0f, 0.0f));
		glDrawArrays(GL_TRIANGLES, 0, 3);
		// Vertical: bloom_tex[1] → bloom_fbo[0]
		glBindFramebuffer(GL_FRAMEBUFFER, m_bloom_fbo[0]);
		glBindTexture(GL_TEXTURE_2D, m_bloom_tex[1]);
		m_blur.setVec2("u_dir", glm::vec2(0.0f, 1.0f));
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	// 3) Composite to default FBO.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, w, h);
	m_composite.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_scene_tex);
	m_composite.setInt("u_scene", 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_bloom_tex[0]);
	m_composite.setInt("u_bloom", 1);
	m_composite.setFloat("u_bloom_strength",    bloom_strength);
	m_composite.setFloat("u_vignette_strength", vignette);
	m_composite.setFloat("u_low_hp",            low_hp);
	m_composite.setFloat("u_time",              time_seconds);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Restore reasonable state for subsequent HUD draws.
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace civcraft::cellcraft

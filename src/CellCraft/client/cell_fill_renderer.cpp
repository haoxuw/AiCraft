#include "CellCraft/client/cell_fill_renderer.h"

#include <algorithm>
#include <cstddef>

namespace civcraft::cellcraft {

bool CellFillRenderer::init() {
	if (!m_shader.loadFromFile("shaders/cell_body.vert",
	                           "shaders/cell_body.frag")) return false;

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	glEnableVertexAttribArray(0);  // pos
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      (void*)offsetof(Vertex, pos));
	glEnableVertexAttribArray(1);  // inset
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      (void*)offsetof(Vertex, inset));
	glEnableVertexAttribArray(2);  // uv
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      (void*)offsetof(Vertex, uv));
	glBindVertexArray(0);
	return true;
}

void CellFillRenderer::shutdown() {
	if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
}

void CellFillRenderer::drawFill(const std::vector<glm::vec2>& poly,
                                const glm::vec3& base_color,
                                sim::Diet diet,
                                float noise_seed,
                                float time_seconds,
                                int screen_w, int screen_h) {
	const size_t n = poly.size();
	if (n < 3) return;

	// Centroid (average).
	glm::vec2 c(0.0f);
	for (auto& v : poly) c += v;
	c /= (float)n;

	// Bbox → UV normalization.
	glm::vec2 mn = poly[0], mx = poly[0];
	for (auto& v : poly) { mn = glm::min(mn, v); mx = glm::max(mx, v); }
	glm::vec2 ext = mx - mn;
	ext.x = std::max(ext.x, 1e-3f);
	ext.y = std::max(ext.y, 1e-3f);

	// Build fan triangulation: for each edge (v_i, v_{i+1}), one triangle
	// (c, v_i, v_{i+1}). Upload as GL_TRIANGLES. Center vertex repeats per
	// triangle but the mesh size is small (3*N verts) and this keeps the
	// shader simple (no primitive-restart).
	m_scratch.clear();
	m_scratch.reserve(n * 3);

	auto make_vert = [&](glm::vec2 p, float inset) {
		Vertex v;
		v.pos   = p;
		v.inset = inset;
		v.uv    = (p - mn) / ext;
		return v;
	};

	for (size_t i = 0; i < n; ++i) {
		const glm::vec2& a = poly[i];
		const glm::vec2& b = poly[(i + 1) % n];
		m_scratch.push_back(make_vert(c, 1.0f));
		m_scratch.push_back(make_vert(a, 0.0f));
		m_scratch.push_back(make_vert(b, 0.0f));
	}

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_shader.use();
	m_shader.setVec2("u_resolution", glm::vec2((float)screen_w, (float)screen_h));
	m_shader.setVec3("u_base_color", base_color);
	m_shader.setVec3("u_diet_color", dietColor(diet));
	m_shader.setFloat("u_noise_seed", noise_seed);
	m_shader.setFloat("u_time", time_seconds);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             (GLsizeiptr)(m_scratch.size() * sizeof(Vertex)),
	             m_scratch.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_scratch.size());
	glBindVertexArray(0);
}

} // namespace civcraft::cellcraft

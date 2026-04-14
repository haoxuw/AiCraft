#include "CellCraft/client/chalk_renderer.h"

namespace civcraft::cellcraft {

bool ChalkRenderer::init() {
	if (!m_board.loadFromFile("shaders/board.vert", "shaders/board.frag")) return false;
	if (!m_chalk.loadFromFile("shaders/chalk.vert", "shaders/chalk.frag")) return false;

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	glEnableVertexAttribArray(0);  // pos
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex),
	                      (void*)offsetof(RibbonVertex, pos));
	glEnableVertexAttribArray(1);  // across
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex),
	                      (void*)offsetof(RibbonVertex, across));
	glEnableVertexAttribArray(2);  // along
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex),
	                      (void*)offsetof(RibbonVertex, along));

	glGenVertexArrays(1, &m_board_vao);
	return true;
}

void ChalkRenderer::shutdown() {
	if (m_vbo)        { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	if (m_vao)        { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
	if (m_board_vao)  { glDeleteVertexArrays(1, &m_board_vao); m_board_vao = 0; }
}

void ChalkRenderer::drawBoard(int w, int h, float t) {
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	m_board.use();
	m_board.setVec2("u_resolution", glm::vec2((float)w, (float)h));
	m_board.setFloat("u_time", t);
	glBindVertexArray(m_board_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ChalkRenderer::drawOne(const ChalkStroke& s, int w, int h) {
	if (s.points.size() < 2) return;
	std::vector<RibbonVertex> mesh;
	s.buildRibbon(mesh);
	if (mesh.empty()) return;

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mesh.size() * sizeof(RibbonVertex)),
	             mesh.data(), GL_STREAM_DRAW);

	m_chalk.setVec2("u_resolution", glm::vec2((float)w, (float)h));
	m_chalk.setVec3("u_color", s.color);
	m_chalk.setFloat("u_half_width", s.half_width);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)mesh.size());
}

void ChalkRenderer::drawStrokes(const std::vector<ChalkStroke>& done,
                                const ChalkStroke* live,
                                int w, int h) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	m_chalk.use();
	glBindVertexArray(m_vao);
	for (const auto& s : done) drawOne(s, w, h);
	if (live) drawOne(*live, w, h);
}

} // namespace civcraft::cellcraft

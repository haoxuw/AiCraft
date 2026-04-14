#pragma once

#include "LifeCraft/client/chalk_stroke.h"
#include "client/shader.h"
#include "client/gl.h"
#include <vector>

namespace civcraft::lifecraft {

// Renders a list of ChalkStrokes to the current framebuffer. Also renders the
// procedural chalkboard background (no external texture required). Uses a
// single VBO that is re-uploaded each frame with the union of all ribbon
// meshes — fine for MVP-scale drawings (hundreds of short strokes); we'll
// cache static strokes into a separate VBO once perf matters.
class ChalkRenderer {
public:
	bool init();
	void shutdown();

	void drawBoard(int screen_w, int screen_h, float time_seconds);
	void drawStrokes(const std::vector<ChalkStroke>& done,
	                 const ChalkStroke* live_preview,
	                 int screen_w, int screen_h);

private:
	void drawOne(const ChalkStroke& s, int screen_w, int screen_h);

	Shader m_board;
	Shader m_chalk;
	GLuint m_vao = 0;
	GLuint m_vbo = 0;
	GLuint m_board_vao = 0;  // empty VAO required by core profile for attrib-less draws
};

} // namespace civcraft::lifecraft

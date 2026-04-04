#pragma once

#include "client/gl.h"
#include <glm/glm.hpp>
#include <string>
#include "client/shader.h"

namespace agentica {

// Minimal bitmap font renderer. 8x8 pixel font, ASCII 32-126.
// Font data from font8x8_basic (public domain).
class TextRenderer {
public:
	bool init(const std::string& shaderDir);
	void shutdown();

	// Draw text in normalized screen coords (-1 to 1).
	// scale: 1.0 = ~24px on a 900p screen
	void drawText(const std::string& text, float x, float y,
	              float scale, glm::vec4 color, float aspect);

	// Draw title text with outline and glow effect
	void drawTitle(const std::string& text, float x, float y,
	               float scale, glm::vec4 color, float aspect);

	// Draw a filled rectangle in NDC coords
	void drawRect(float x, float y, float w, float h, glm::vec4 color);

	// Draw a filled arc/ring segment in NDC coords.
	// cx, cy: center; r_inner/r_outer: radii in NDC-x units (r_inner=0 → filled disk).
	// Angles in radians (0=right, counterclockwise). aspect corrects for non-square screens.
	void drawArc(float cx, float cy, float r_inner, float r_outer,
	             float startAngle, float endAngle,
	             glm::vec4 color, float aspect, int segments = 40);

private:
	void generateFontTexture();

	Shader m_textShader;
	GLuint m_fontTexture = 0;
	GLuint m_vao = 0, m_vbo = 0;
};

// Classic 8x8 bitmap font (public domain font8x8_basic)
extern const unsigned char FONT_8X8[96][8];

} // namespace agentica

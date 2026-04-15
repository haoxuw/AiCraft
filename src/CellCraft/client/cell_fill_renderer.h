// CellCraft — organic body fill pass.
//
// Draws a watercolor-wash fill inside a cell polygon using a fan
// triangulation from the centroid. The existing ChalkRenderer outline
// runs on top to preserve the hand-drawn look.
//
// Visual tuning lives here so a designer can tweak without touching the
// shader. The shader itself encodes the VISUAL MODEL (membrane→cytoplasm
// gradient + noise + shimmer); constants on this side are knobs.
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/sim/part.h"
#include "client/gl.h"
#include "client/shader.h"

namespace civcraft::cellcraft {

// Tunable constants — see shaders/cell_body.frag for the model.
// Uniforms with the same name override these when set (the shader reads
// its uniforms, not these — keeping these here is for host-side code +
// future UI tuning).
constexpr float MEMBRANE_WIDTH        = 0.22f;
constexpr float CYTOPLASM_BRIGHTNESS  = 1.15f;
constexpr float MEMBRANE_DARKNESS     = 0.55f;
constexpr float NOISE_STRENGTH        = 0.15f;
constexpr float DIET_TINT_MIX         = 0.70f;
constexpr glm::vec3 DIET_CARNIVORE = {0.937f, 0.353f, 0.435f};  // #EF5A6F
constexpr glm::vec3 DIET_HERBIVORE = {0.439f, 0.788f, 0.435f};  // #70C96F
constexpr glm::vec3 DIET_OMNIVORE  = {0.722f, 0.463f, 0.910f};  // #B876E8

inline glm::vec3 dietColor(sim::Diet d) {
	switch (d) {
		case sim::Diet::CARNIVORE: return DIET_CARNIVORE;
		case sim::Diet::HERBIVORE: return DIET_HERBIVORE;
		case sim::Diet::OMNIVORE:  return DIET_OMNIVORE;
	}
	return DIET_OMNIVORE;
}

class CellFillRenderer {
public:
	bool init();
	void shutdown();

	// polygon_px: closed polygon vertices in screen-pixel space, NOT
	// including a repeated first vertex. Must have >= 3 points.
	void drawFill(const std::vector<glm::vec2>& polygon_px,
	              const glm::vec3& base_color,
	              sim::Diet diet,
	              float noise_seed,
	              float time_seconds,
	              int screen_w, int screen_h,
	              float diet_mix = 0.7f,
	              float alpha_scale = 1.0f);

private:
	struct Vertex {
		glm::vec2 pos;
		float     inset;
		glm::vec2 uv;
	};

	Shader m_shader;
	GLuint m_vao = 0;
	GLuint m_vbo = 0;
	std::vector<Vertex> m_scratch;
};

} // namespace civcraft::cellcraft

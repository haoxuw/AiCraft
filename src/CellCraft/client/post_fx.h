// CellCraft — post-processing pipeline.
//
// Pipeline:
//   begin(w,h)                  → bind scene FBO, caller renders scene into it
//   render_to_default()         → bloom extract → ping-pong gaussian blur →
//                                 composite (scene + bloom + vignette) into
//                                 default framebuffer.
//
// Scene FBO is at native resolution; bloom buffers are at half resolution for
// speed and for a softer halo. Resize is lazy: begin() detects size changes.

#pragma once

#include "client/shader.h"
#include "client/gl.h"

namespace civcraft::cellcraft {

class PostFX {
public:
	bool init();
	void shutdown();

	// Must be called each frame before rendering the scene.
	// Binds the scene FBO and clears it to the chalkboard base color.
	void begin(int screen_w, int screen_h);

	// Runs bloom + composite, writing to the default framebuffer (0).
	// Call after all scene rendering is done, before HUD/text overlays.
	// (HUD is drawn AFTER composite so text stays crisp.)
	void render_to_default(int screen_w, int screen_h, float time_seconds);

	// Tunables.
	// Tuned for bright pastel board: threshold tuned above paper luminance,
	// strength low so saturated strokes just kiss the highlights rather
	// than washing the scene out. Vignette softened but kept for depth.
	float bloom_threshold = 1.05f;
	float bloom_strength  = 0.25f;
	float vignette        = 0.30f;
	float low_hp          = 0.0f;  // 0..1 red-edge pulse

private:
	void ensure_size_(int w, int h);
	void destroy_buffers_();

	int  m_w = 0, m_h = 0;
	int  m_bw = 0, m_bh = 0; // bloom (half-res)

	// Scene FBO + color texture.
	GLuint m_scene_fbo = 0;
	GLuint m_scene_tex = 0;

	// Bloom ping-pong FBOs at half-res.
	GLuint m_bloom_fbo[2] = {0, 0};
	GLuint m_bloom_tex[2] = {0, 0};

	// Empty VAO for attrib-less fullscreen triangle.
	GLuint m_vao = 0;

	Shader m_extract;
	Shader m_blur;
	Shader m_composite;
};

} // namespace civcraft::cellcraft

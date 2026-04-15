#version 410 core

// CellCraft — organic cell body fill.
// Fan-triangulated polygon: one centroid vertex (inset=1), one ring of
// edge vertices (inset=0). The fragment shader interpolates inset across
// the triangle to produce a membrane → cytoplasm radial gradient.

layout(location = 0) in vec2  a_pos;    // pixel space
layout(location = 1) in float a_inset;  // 0 at edge, 1 at centroid
layout(location = 2) in vec2  a_uv;     // normalized position in bbox, for noise

uniform vec2 u_resolution;

out float v_inset;
out vec2  v_uv;

void main() {
	vec2 clip = (a_pos / u_resolution) * 2.0 - 1.0;
	clip.y = -clip.y;  // window y-down → clip y-up
	gl_Position = vec4(clip, 0.0, 1.0);
	v_inset = a_inset;
	v_uv    = a_uv;
}

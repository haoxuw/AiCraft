#version 410 core

// Feathered-ribbon vertex shader for chalk strokes.
// CPU uploads a ribbon as two rows of vertices along the polyline: each
// input vertex carries its pixel position, an across-width coord (-1 at
// left edge, +1 at right edge), and an along-length coord (stroke pixels
// travelled from p0). The fragment shader uses the across coord for
// feathering and the length coord for grit variation.

layout(location = 0) in vec2 a_pos;     // pixel space
layout(location = 1) in float a_across; // -1 .. +1 across ribbon width
layout(location = 2) in float a_along;  // length in pixels along stroke

uniform vec2 u_resolution;

out float v_across;
out float v_along;

void main() {
	vec2 clip = (a_pos / u_resolution) * 2.0 - 1.0;
	clip.y = -clip.y;  // window coords (y-down) → clip (y-up)
	gl_Position = vec4(clip, 0.0, 1.0);
	v_across = a_across;
	v_along  = a_along;
}

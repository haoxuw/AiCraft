#version 410 core

// SdfFont vertex shader — converts pixel-space quads to NDC.
// Pixel origin is top-left, y-down.
//
// Each vertex carries its glyph's tight uv box as a bounds attribute so the
// fragment shader can clamp glow-blur samples to the glyph's own cell and
// never leak into neighbor glyphs in the packed atlas.

layout(location = 0) in vec2 a_pos_px;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_uv_box; // (u0, v0, u1, v1)

uniform vec2 u_screen_size;

out vec2 v_uv;
out vec4 v_uv_box;

void main() {
	vec2 ndc;
	ndc.x =  (a_pos_px.x / u_screen_size.x) * 2.0 - 1.0;
	ndc.y = -(a_pos_px.y / u_screen_size.y) * 2.0 + 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	v_uv = a_uv;
	v_uv_box = a_uv_box;
}

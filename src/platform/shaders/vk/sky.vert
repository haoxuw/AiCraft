#version 450

// Pass NDC linearly across the triangle. The fragment shader reconstructs the
// world-space ray direction per pixel from invVP — the perspective divide
// makes the world ray non-linear in NDC, so we cannot pre-compute it here.

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir;       // xyz + sunStr
	vec4 cloudParams;  // camX, camY, camZ, time
} pc;

layout(location = 0) out vec2 vNDC;

void main() {
	// Fullscreen triangle: 3 vertices cover clip space without a VBO.
	// All vertices share gl_Position.w = 1, so vNDC interpolates linearly
	// (and exactly equals the screen NDC) at every fragment.
	vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
	gl_Position = vec4(pos, 0.999, 1.0);
	vNDC = pos;
}

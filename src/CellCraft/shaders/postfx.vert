#version 410 core
// Fullscreen triangle — no vertex attributes. Emits a single triangle
// that covers the viewport when drawn with GL_TRIANGLES, 3 verts.
out vec2 v_uv;
void main() {
	vec2 p = vec2((gl_VertexID == 2) ? 3.0 : -1.0,
	              (gl_VertexID == 1) ? 3.0 : -1.0);
	v_uv = p * 0.5 + 0.5;
	gl_Position = vec4(p, 0.0, 1.0);
}

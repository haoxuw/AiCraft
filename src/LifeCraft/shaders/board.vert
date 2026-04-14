#version 410 core

// Full-screen triangle. Drawn with glDrawArrays(GL_TRIANGLES, 0, 3) and no VBO;
// positions are synthesized from gl_VertexID. Produces a tri that covers the
// viewport [-1,+1]^2 and passes NDC coords as uv for the fragment shader.
out vec2 v_uv;

void main() {
	vec2 p = vec2(
		(gl_VertexID == 1) ?  3.0 : -1.0,
		(gl_VertexID == 2) ?  3.0 : -1.0
	);
	v_uv = p;
	gl_Position = vec4(p, 0.0, 1.0);
}

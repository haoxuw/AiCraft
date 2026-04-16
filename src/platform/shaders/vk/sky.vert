#version 450

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir; // xyz = sunDir, w = sunStrength
} pc;

layout(location = 0) out vec3 vRayDir;

void main() {
	// Fullscreen triangle: 3 vertices cover clip space without a VBO.
	vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
	gl_Position = vec4(pos, 0.999, 1.0);

	vec4 worldPos = pc.invVP * vec4(pos, 1.0, 1.0);
	vRayDir = normalize(worldPos.xyz / worldPos.w);
}

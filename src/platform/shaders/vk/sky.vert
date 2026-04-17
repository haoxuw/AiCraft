#version 450

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir;    // xyz = sunDir, w = sunStrength
	vec4 skyParams; // x = timeSec, yzw reserved (weather/season)
} pc;

layout(location = 0) out vec3 vRayDir;

void main() {
	// Fullscreen triangle: 3 vertices cover clip space without a VBO.
	vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
	gl_Position = vec4(pos, 0.999, 1.0);

	// Ray direction in world space = far - near, not just world-pos normalized
	// from the origin. The old path broke when the camera was off-origin (all
	// screen pixels produced near-identical directions, killing the cloud noise
	// spatial variation).
	vec4 nearW = pc.invVP * vec4(pos, 0.0, 1.0);
	vec4 farW  = pc.invVP * vec4(pos, 1.0, 1.0);
	vRayDir    = normalize(farW.xyz / farW.w - nearW.xyz / nearW.w);
}

#version 450

// Box-model vertex shader. Per-instance oriented box: a 4x4 model matrix
// mapping the unit cube [0,1]^3 into its world-space part, plus an RGB tint.
// This replaces the older {worldPos, size, color} AA-only format — with a
// full matrix, per-part rotations (walk-cycle limb swings, melee keyframes,
// head tracking, item equip transforms) survive the instanced batch.

// Per-vertex (unit cube [0,1]^3)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
// Per-instance — a mat4 is four vec4 attribute slots (GLSL convention).
layout(location = 2) in vec4 inModelRow0;
layout(location = 3) in vec4 inModelRow1;
layout(location = 4) in vec4 inModelRow2;
layout(location = 5) in vec4 inModelRow3;
layout(location = 6) in vec3 inColor;

layout(push_constant) uniform PC {
	mat4 viewProj;
	vec4 camPos;   // xyz = camera world pos, w = time
	vec4 sunDir;   // xyz = sun direction (normalized), w = sunStrength
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out float vDist;

void main() {
	// GLM column-major: four vec4 rows give us the model matrix directly.
	mat4 model = mat4(inModelRow0, inModelRow1, inModelRow2, inModelRow3);
	vec4 world4 = model * vec4(inPos, 1.0);
	gl_Position = pc.viewProj * world4;
	vColor = inColor;
	// Cube face normals are axis-aligned; the upper 3x3 of the model matrix
	// contains rotation + per-axis scale. For axis-aligned input normals this
	// preserves direction after normalize (scale along an axis doesn't change
	// the direction of vectors on that axis).
	vNormal = normalize(mat3(model) * inNormal);
	vWorldPos = world4.xyz;
	vDist = length(world4.xyz - pc.camPos.xyz);
}

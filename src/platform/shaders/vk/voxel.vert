#version 450

// Per-vertex (unit cube)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
// Per-instance
layout(location = 2) in vec3 inInstPos;
layout(location = 3) in vec3 inInstColor;

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
	vec3 world = inPos + inInstPos;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vColor = inInstColor;
	vNormal = inNormal;
	vWorldPos = world;
	vDist = length(world - pc.camPos.xyz);
}

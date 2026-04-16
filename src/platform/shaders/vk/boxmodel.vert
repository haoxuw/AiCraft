#version 450

// Box-model vertex shader. Generalizes the voxel pipeline: per-instance
// {worldPos, size, color} instead of unit-size cubes. Used for rendering
// entities (creatures, players, items) as compositions of axis-aligned
// boxes in world space. Pairs with voxel.frag (identical vertex outputs).

// Per-vertex (unit cube [0,1]^3)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
// Per-instance
layout(location = 2) in vec3 inWorldPos;  // box min-corner in world space
layout(location = 3) in vec3 inSize;      // box size along each axis
layout(location = 4) in vec3 inColor;

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
	vec3 world = inWorldPos + inPos * inSize;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vColor = inColor;
	vNormal = inNormal;
	vWorldPos = world;
	vDist = length(world - pc.camPos.xyz);
}

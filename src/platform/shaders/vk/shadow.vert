#version 450

// Depth-only shadow pass for instanced voxels. Re-uses the voxel vertex
// layout so the same cube VBO + instance buffer can be bound.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;     // unused; kept for layout parity
layout(location = 2) in vec3 inInstPos;
layout(location = 3) in vec3 inInstColor;  // unused; kept for layout parity

layout(push_constant) uniform PC {
	mat4 shadowVP;
} pc;

void main() {
	vec3 world = inPos + inInstPos;
	gl_Position = pc.shadowVP * vec4(world, 1.0);
}

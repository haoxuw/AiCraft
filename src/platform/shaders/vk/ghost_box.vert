#version 450

// Translucent ghost box for the block-placement preview. Same unit-cube
// input as crack_overlay — stamped to the AABB pushed in PC.

layout(push_constant) uniform PC {
	mat4  viewProj;
	vec4  aabbMin;   // xyz = min corner, w unused
	vec4  aabbMax;   // xyz = max corner, w unused
	vec4  color;     // rgba, premultiplied by the caller if desired
} pc;

layout(location = 0) in vec3 inLocalPos;   // [0,1]^3 unit cube corner
layout(location = 0) out vec3 vLocalPos;

void main() {
	vec3 size = pc.aabbMax.xyz - pc.aabbMin.xyz;
	vec3 world = pc.aabbMin.xyz + inLocalPos * size;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vLocalPos = inLocalPos;
}

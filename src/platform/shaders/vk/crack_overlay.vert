#version 450

// Stamps the unit cube vertex buffer at the target block's AABB (pushed
// in PC). localPos stays [0,1]^3 so the frag can reason in AABB-local
// space and derive face-UVs from it.

layout(push_constant) uniform PC {
	mat4  viewProj;
	vec4  aabbMin;    // xyz = min corner, w = stage (0,1,2)
	vec4  aabbMax;    // xyz = max corner, w = time (seconds)
} pc;

layout(location = 0) in vec3 inLocalPos;   // [0,1]^3 unit cube corner

layout(location = 0) out vec3 vLocalPos;

void main() {
	// 1% inflation outward along each axis so the overlay sits just past
	// the block face — stops z-fighting with the chunk mesh.
	vec3 inflated = (inLocalPos - 0.5) * 1.01 + 0.5;
	vec3 size = pc.aabbMax.xyz - pc.aabbMin.xyz;
	vec3 world = pc.aabbMin.xyz + inflated * size;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vLocalPos = inLocalPos;
}

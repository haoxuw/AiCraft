#version 450

// Unit-cube overlay centered on a block. The cube's 8 unique corners are
// encoded as the low 3 bits of gl_VertexIndex per tri (expanded CPU-side
// with a 36-index triangle soup of a unit cube at [0,1]^3). Vertex shader
// just translates by the block's world position and emits local coords
// so the fragment can reason in face-UV space.

layout(push_constant) uniform PC {
	mat4  viewProj;
	vec4  blockPos;   // xyz = integer corner of the block, w = stage (0,1,2)
	vec4  params;     // x = time (seconds), y..w reserved
} pc;

layout(location = 0) in vec3 inLocalPos;   // in [0,1]^3 — unit cube corner

layout(location = 0) out vec3 vLocalPos;

void main() {
	// Inflate 1% so the overlay sits just outside the block — eliminates
	// z-fighting with the chunk mesh and keeps the pattern visible at
	// grazing angles. localPos stays [0,1] for face-UV math in the frag.
	vec3 inflated = (inLocalPos - 0.5) * 1.01 + 0.5;
	vec3 world = pc.blockPos.xyz + inflated;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vLocalPos = inLocalPos;
}

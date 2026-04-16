#version 450

// Billboarded particle. One instance = one particle; we emit a 4-vertex
// triangle strip per instance via gl_VertexIndex (0..3 = quad corners),
// screen-aligned and sized in world units. No per-vertex VBO — all data
// comes from the per-instance attributes + gl_VertexIndex.

layout(location = 0) in vec3 inWorldPos;   // particle center in world space
layout(location = 1) in float inSize;      // world-space radius
layout(location = 2) in vec4 inColor;      // RGB + alpha (alpha used by frag)

layout(push_constant) uniform PC {
	mat4 viewProj;
	vec4 camRight;  // view-space right vector, xyz
	vec4 camUp;     // view-space up vector, xyz
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
	// gl_VertexIndex 0..3 → quad corners in UV ±1 space.
	// Triangle strip order: (-1,-1), (+1,-1), (-1,+1), (+1,+1).
	vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
	                   (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
	vec3 worldOffset = pc.camRight.xyz * corner.x * inSize
	                 + pc.camUp.xyz    * corner.y * inSize;
	vec3 world = inWorldPos + worldOffset;
	gl_Position = pc.viewProj * vec4(world, 1.0);
	vUV = corner;                 // [-1, 1] for radial falloff
	vColor = inColor;
}

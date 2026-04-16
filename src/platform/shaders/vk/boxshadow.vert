#version 450

// Depth-only shadow pass for box-model instances (9 floats per instance).
// Mirrors boxmodel.vert's position math but writes only depth from the sun's
// point of view. Reuses shadow.frag (empty main) for the fragment stage.

layout(location = 0) in vec3 inPos;       // unit cube [0,1]^3
layout(location = 1) in vec3 inNormal;    // unused; kept for layout parity
layout(location = 2) in vec3 inWorldPos;  // box min-corner in world
layout(location = 3) in vec3 inSize;
layout(location = 4) in vec3 inColor;     // unused; kept for layout parity

layout(push_constant) uniform PC {
	mat4 shadowVP;
} pc;

void main() {
	vec3 world = inWorldPos + inPos * inSize;
	gl_Position = pc.shadowVP * vec4(world, 1.0);
}

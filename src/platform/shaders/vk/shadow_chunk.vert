#version 450

// Depth-only shadow pass for chunk_mesher per-vertex meshes (13 floats per
// vertex). The chunk-mesh vertex layout puts world-space position at
// location 0 — we ignore everything else and just project into shadow space.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;     // unused; kept for layout parity
layout(location = 2) in vec3 inNormal;    // unused; kept for layout parity
layout(location = 3) in float inAO;       // unused; kept for layout parity
layout(location = 4) in float inShade;    // unused; kept for layout parity
layout(location = 5) in float inAlpha;    // unused; kept for layout parity
layout(location = 6) in float inGlow;     // unused; kept for layout parity

layout(push_constant) uniform PC {
	mat4 shadowVP;
} pc;

void main() {
	gl_Position = pc.shadowVP * vec4(inPos, 1.0);
}

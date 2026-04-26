#version 450

// Per-vertex chunk mesh — emitted by Solarium's chunk_mesher (and any port
// thereof). 13 floats per vertex, no instancing.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inAO;
layout(location = 4) in float inShade;
layout(location = 5) in float inAlpha;
layout(location = 6) in float inGlow;

// 128-byte push constant (Vulkan minimum guaranteed range).
layout(push_constant) uniform PC {
	mat4 viewProj;     // 64
	vec4 camPos;       // xyz=cam, w=time            (16)
	vec4 sunDir;       // xyz=sun, w=sunStrength     (16)
	vec4 fog;          // rgb=fogColor, a=fogStart   (16)
	vec4 fogExtra;     // x=fogEnd, yzw=pad          (16)
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out float vAO;
layout(location = 4) out float vShade;
layout(location = 5) out float vAlpha;
layout(location = 6) out float vGlow;

void main() {
	vWorldPos = inPos;
	vColor    = inColor;
	vNormal   = inNormal;
	vAO       = inAO;
	vShade    = inShade;
	vAlpha    = inAlpha;
	vGlow     = inGlow;
	gl_Position = pc.viewProj * vec4(inPos, 1.0);
}

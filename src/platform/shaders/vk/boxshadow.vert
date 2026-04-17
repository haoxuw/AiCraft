#version 450

// Depth-only shadow pass for box-model instances. Reads the same 19-float
// per-instance record as boxmodel.vert (mat4 model + vec3 color) so character
// shadows stay in sync with lit geometry — the color channel is unused here
// but included for layout parity with the lit pipeline.

layout(location = 0) in vec3 inPos;       // unit cube [0,1]^3
layout(location = 1) in vec3 inNormal;    // unused; kept for layout parity
layout(location = 2) in vec4 inModelRow0;
layout(location = 3) in vec4 inModelRow1;
layout(location = 4) in vec4 inModelRow2;
layout(location = 5) in vec4 inModelRow3;
layout(location = 6) in vec3 inColor;     // unused; kept for layout parity

layout(push_constant) uniform PC {
	mat4 shadowVP;
} pc;

void main() {
	mat4 model = mat4(inModelRow0, inModelRow1, inModelRow2, inModelRow3);
	gl_Position = pc.shadowVP * model * vec4(inPos, 1.0);
}

#version 450

// Camera-aligned ribbon strip. Input is a pre-expanded vertex buffer — CPU
// has already taken each control point, computed the side vector (normal
// of tangent × viewDir), and emitted two vertices offset ±width/2 along
// it. We just transform and pass the colour through. Triangle-strip
// topology means consecutive pairs form quads automatically.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PC {
	mat4 viewProj;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
	gl_Position = pc.viewProj * vec4(inPos, 1.0);
	vColor = inColor;
}

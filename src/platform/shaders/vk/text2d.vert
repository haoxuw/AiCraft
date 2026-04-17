#version 450

// 2D UI primitive. CPU emits NDC with +y up; flip here to map onto
// Vulkan's +y-down clip space.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

void main() {
	gl_Position = vec4(inPos.x, -inPos.y, 0.0, 1.0);
	vUV = inUV;
}

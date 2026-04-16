#version 450

// Premultiplied additive — same convention as particles so the same bloom
// + tonemap path treats the ribbon as glowing emissive material.

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
	float a = vColor.a;
	outColor = vec4(vColor.rgb * a, a);
}

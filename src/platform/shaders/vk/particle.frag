#version 450

// Soft radial particle. Discard outside unit circle (vUV is ±1 space), fade
// with a smoothstep so edges aren't aliased. Additive blend happens in the
// pipeline state, not the shader — we just output premultiplied RGB*A.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
	float r = length(vUV);
	if (r > 1.0) discard;
	// Soft inner glow: quadratic falloff gives a bright core + fuzzy edge,
	// which bloom then picks up cleanly.
	float alpha = vColor.a * pow(1.0 - r, 1.6);
	outColor = vec4(vColor.rgb * alpha, alpha);
}

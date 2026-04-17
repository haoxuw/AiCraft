#version 450

// 2D UI fragment: SDF text / solid fill / SDF title (outline + glow).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uFontTex;

layout(push_constant) uniform PC {
	vec4 uColor;  // rgb = tint, a = alpha multiplier
	int  uMode;   // 0 = SDF text, 1 = solid rect, 2 = title (outline + glow)
	int  _pad0;
	int  _pad1;
	int  _pad2;
} pc;

void main() {
	if (pc.uMode == 1) {
		outColor = pc.uColor;
		return;
	}

	float dist = texture(uFontTex, vUV).r;

	// Screen-space derivative → resolution-independent AA.
	float smoothing = fwidth(dist) * 1.2 + 0.02;
	float edge = 0.5;

	vec3 tint = pc.uColor.rgb;
	float alphaMul = pc.uColor.a;

	if (pc.uMode == 0) {
		float a = smoothstep(edge - smoothing, edge + smoothing, dist);
		if (a < 0.01) discard;
		outColor = vec4(tint, alphaMul * a);
		return;
	}

	// Title: fill + dark outline + soft glow.
	float fillAlpha = smoothstep(edge - smoothing, edge + smoothing, dist);

	float outlineEdge = 0.33;
	float outlineAlpha = smoothstep(outlineEdge - smoothing, outlineEdge + smoothing, dist);
	vec3 outlineColor = tint * 0.15;

	float glowEdge = 0.18;
	float glowAlpha = smoothstep(glowEdge, edge, dist) * 0.25;
	vec3 glowColor = tint * 0.5;

	vec3 color = glowColor;
	float a = glowAlpha;
	color = mix(color, outlineColor, outlineAlpha);
	a = max(a, outlineAlpha * 0.85);
	color = mix(color, tint, fillAlpha);
	a = max(a, fillAlpha);

	if (a < 0.01) discard;
	outColor = vec4(color, alphaMul * a);
}

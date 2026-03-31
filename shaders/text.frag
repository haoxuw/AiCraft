#version 410 core

in vec2 vUV;

uniform sampler2D uFontTex;
uniform vec3 uColor;
uniform float uAlpha;
uniform int uMode; // 0 = SDF text, 1 = solid rect, 2 = title (outline+glow)

out vec4 fragColor;

void main() {
	if (uMode == 1) {
		// Solid rectangle
		fragColor = vec4(uColor, uAlpha);
		return;
	}

	float dist = texture(uFontTex, vUV).r;

	// Screen-space derivative for resolution-independent anti-aliasing
	float smoothing = fwidth(dist) * 1.2 + 0.02;
	float edge = 0.5;

	if (uMode == 0) {
		// Regular SDF text: smooth anti-aliased edges
		float alpha = smoothstep(edge - smoothing, edge + smoothing, dist);
		if (alpha < 0.01) discard;
		fragColor = vec4(uColor, uAlpha * alpha);

	} else {
		// Title text: fill + outline + soft glow

		// Inner fill
		float fillAlpha = smoothstep(edge - smoothing, edge + smoothing, dist);

		// Outline band (drawn at a wider threshold)
		float outlineEdge = 0.33;
		float outlineAlpha = smoothstep(outlineEdge - smoothing, outlineEdge + smoothing, dist);
		vec3 outlineColor = uColor * 0.15; // dark outline

		// Soft outer glow
		float glowEdge = 0.18;
		float glowAlpha = smoothstep(glowEdge, edge, dist) * 0.25;
		vec3 glowColor = uColor * 0.5;

		// Composite: glow behind outline behind fill
		vec3 color = glowColor;
		float alpha = glowAlpha;

		// Blend outline on top
		color = mix(color, outlineColor, outlineAlpha);
		alpha = max(alpha, outlineAlpha * 0.85);

		// Blend fill on top
		color = mix(color, uColor, fillAlpha);
		alpha = max(alpha, fillAlpha);

		if (alpha < 0.01) discard;
		fragColor = vec4(color, uAlpha * alpha);
	}
}

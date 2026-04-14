#version 410 core

// Black board with subtle drifting cloud currents. Domain-warped FBM
// produces slow rolling shapes, but amplitude is kept low so the board
// reads as near-black — clouds are a hint, not a feature.

in vec2 v_uv;
out vec4 f_color;

uniform vec2  u_resolution;
uniform float u_time;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float a = hash12(i);
	float b = hash12(i + vec2(1, 0));
	float c = hash12(i + vec2(0, 1));
	float d = hash12(i + vec2(1, 1));
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
	float v = 0.0;
	float amp = 0.5;
	for (int i = 0; i < 5; ++i) {
		v += amp * vnoise(p);
		p = p * 2.07 + vec2(11.3, 7.1);
		amp *= 0.55;
	}
	return v;
}

void main() {
	vec2 pix = gl_FragCoord.xy;
	vec2 uv  = pix / u_resolution.y;

	// Domain warp: two slow-drifting FBM fields, second one warps the
	// first. Creates the looped "rolling clouds" look without hard edges.
	vec2 q = vec2(
		fbm(uv * 1.4 + vec2( u_time * 0.04,  u_time * 0.02)),
		fbm(uv * 1.4 + vec2(-u_time * 0.03,  u_time * 0.05) + 5.2)
	);
	float f = fbm(uv * 2.0 + 3.5 * q + vec2(u_time * 0.06, 0.0));

	// Near-black base with a whisper of cool tint so it doesn't look dead.
	vec3 base = vec3(0.015, 0.020, 0.025);

	// Cloud lift: very small amplitude, centered so f≈0.5 is neutral.
	// 0.06 max lift means the brightest wisps only reach ~vec3(0.07).
	base += vec3(0.05, 0.06, 0.07) * (f - 0.5) * 1.2;

	// Fine pixel tooth — keeps the board from looking flat/gradient-y
	// at monitor resolution without reading as "chalkboard texture".
	float tooth = hash12(floor(pix));
	base += (tooth - 0.5) * 0.010;

	f_color = vec4(max(base, vec3(0.0)), 1.0);
}

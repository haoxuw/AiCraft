#version 410 core

// Bright pastel "paper" board. Warm cream base with soft peach undertones
// and gentle drifting cloud currents — modern kids-game energy (Roblox /
// Fall Guys vibe) rather than chalkboard darkness.

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

	// Domain warp cloud currents, kept for subtle movement.
	vec2 q = vec2(
		fbm(uv * 1.4 + vec2( u_time * 0.04,  u_time * 0.02)),
		fbm(uv * 1.4 + vec2(-u_time * 0.03,  u_time * 0.05) + 5.2)
	);
	float f = fbm(uv * 2.0 + 3.5 * q + vec2(u_time * 0.06, 0.0));

	// Warm cream paper base with soft peach undertones.
	vec3 base = vec3(0.97, 0.94, 0.88);

	// Cloud tint: warm peach in one direction, cool sky-tint the other.
	// Keep amplitude gentle so the surface reads calm, not busy.
	vec3 warm = vec3(0.99, 0.91, 0.82);   // peach
	vec3 cool = vec3(0.93, 0.96, 0.99);   // sky
	base = mix(base, mix(cool, warm, f), 0.18);

	// Soft radial vignette pulling the edges toward a slightly deeper cream
	// — helps UI cards pop without going dark.
	vec2  ndc   = (pix / u_resolution) * 2.0 - 1.0;
	float rad   = length(ndc * vec2(u_resolution.x / u_resolution.y, 1.0));
	float vigs  = smoothstep(0.8, 1.6, rad);
	base = mix(base, vec3(0.91, 0.86, 0.80), vigs * 0.35);

	// Fine paper tooth — keeps a tactile feel without reading as chalkboard.
	float tooth = hash12(floor(pix));
	base += (tooth - 0.5) * 0.012;

	f_color = vec4(clamp(base, vec3(0.0), vec3(1.0)), 1.0);
}

shader_type spatial;
render_mode diffuse_lambert, specular_disabled;

// Texture-free underwater floor: folded-noise caustics layered over a slow
// low-frequency sand color variation. Produces thin crossing light lines
// instead of blobby cells. Reference: iq / shane caustic tricks.

uniform vec3 sand_tint : source_color = vec3(0.38, 0.42, 0.32);
uniform vec3 sand_dark : source_color = vec3(0.22, 0.26, 0.20);
uniform vec3 caustic_tint : source_color = vec3(1.0, 1.05, 0.9);
uniform float caustic_strength = 1.2;
uniform float caustic_speed = 0.22;
uniform vec2 sand_uv_scale = vec2(6.0, 6.0);
uniform vec2 caustic_uv_scale = vec2(0.5, 0.5);

vec2 hash22(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return fract(sin(p) * 43758.5453) * 2.0 - 1.0;
}

float snoise(vec2 p) {
	const float K1 = 0.366025404;
	const float K2 = 0.211324865;
	vec2 i = floor(p + (p.x + p.y) * K1);
	vec2 a = p - i + (i.x + i.y) * K2;
	float m = step(a.y, a.x);
	vec2 o = vec2(m, 1.0 - m);
	vec2 b = a - o + K2;
	vec2 c = a - 1.0 + 2.0 * K2;
	vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
	vec3 n = h * h * h * h * vec3(
		dot(a, hash22(i)),
		dot(b, hash22(i + o)),
		dot(c, hash22(i + 1.0))
	);
	return dot(n, vec3(70.0));
}

float caustics(vec2 uv, float t) {
	vec2 p = uv;
	p += 0.25 * vec2(snoise(p * 1.3 + t * 0.6), snoise(p * 1.3 - t * 0.5));
	float n1 = 1.0 - abs(snoise(p * 2.0 + vec2(0.0, t)));
	float n2 = 1.0 - abs(snoise(p * 4.0 + vec2(t * 0.7, -t * 0.4)));
	return pow(n1, 6.0) * 0.7 + pow(n2, 8.0) * 0.4;
}

void fragment() {
	vec2 uv = UV * sand_uv_scale;
	// Low-frequency blotchy sand base (no texture needed).
	float s = snoise(uv * 0.8) * 0.5 + 0.5;
	s = smoothstep(0.35, 0.85, s);
	vec3 sand = mix(sand_dark, sand_tint, s);

	vec2 cuv = UV * caustic_uv_scale * sand_uv_scale;
	float t = TIME * caustic_speed;
	float c = caustics(cuv, t) * caustic_strength;

	ALBEDO = sand + caustic_tint * c * 0.6;
	ROUGHNESS = 0.95;
	METALLIC = 0.0;
}

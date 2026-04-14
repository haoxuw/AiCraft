shader_type spatial;
render_mode cull_front, unshaded, depth_draw_opaque;

// Water surface viewed from below. Shimmering translucent ceiling with
// animated ripple highlights.

uniform vec3 shallow_color : source_color = vec3(0.35, 0.75, 0.85);
uniform vec3 deep_color : source_color = vec3(0.05, 0.25, 0.35);
uniform float ripple_speed = 0.4;
uniform float ripple_strength = 0.6;

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

void fragment() {
	vec2 uv = UV * 6.0;
	float t = TIME * ripple_speed;
	uv += 0.2 * vec2(snoise(uv * 1.5 + t), snoise(uv * 1.5 - t));
	float ripple = 1.0 - abs(snoise(uv * 2.0 + vec2(0.0, t * 0.8)));
	ripple = pow(ripple, 8.0);
	vec3 col = mix(deep_color, shallow_color, ripple * ripple_strength);
	ALBEDO = col;
	ALPHA = 0.85;
}

#version 410 core

// Organic cell body watercolor-wash fill.
// See client/app.cpp drawMonsters() — this pass runs BEFORE the chalk
// outline, which draws cleanly on top and preserves the hand-drawn feel.

in float v_inset;   // 0 at edge, 1 at centroid
in vec2  v_uv;      // normalized position within cell bbox
out vec4 f_color;

uniform vec3  u_base_color;   // cell palette color
uniform vec3  u_diet_color;   // carnivore red / herbivore green / omnivore purple
uniform float u_noise_seed;   // per-monster, stable across frames
uniform float u_time;         // seconds, for shimmer
uniform float u_diet_mix;     // 0.7 default; background silhouettes pass 0.0
uniform float u_alpha_scale;  // 1.0 default; far layers pass <1.0 for recession

float hash21(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	float a = hash21(i);
	float b = hash21(i + vec2(1.0, 0.0));
	float c = hash21(i + vec2(0.0, 1.0));
	float d = hash21(i + vec2(1.0, 1.0));
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main() {
	// Tint: blend palette color with diet color. BG silhouettes pass
	// u_diet_mix=0 so the base_color alone controls hue.
	vec3 organism = mix(u_base_color, u_diet_color, u_diet_mix);

	// Membrane (darker edge) vs cytoplasm (lighter center).
	vec3 cytoplasm = organism * 1.15;
	vec3 membrane  = organism * 0.55;
	float t = clamp(v_inset, 0.0, 1.0);
	float membrane_width = 0.22;
	float w = smoothstep(0.0, membrane_width, t);
	vec3 color = mix(membrane, cytoplasm, w);

	// 2-octave value noise for watercolor speckling, 15% strength.
	float n1 = valueNoise(v_uv * 3.0 + vec2(u_noise_seed));
	float n2 = valueNoise(v_uv * 8.0 + vec2(u_noise_seed * 2.3)) * 0.5;
	float noise = (n1 + n2) / 1.5 - 0.5;
	color *= (1.0 + noise * 0.15);

	// Subtle animated shimmer at the cytoplasm — "alive".
	float shimmer = 0.04 * sin(u_time * 1.5 + v_uv.x * 6.0 + u_noise_seed);
	color += vec3(shimmer) * w;

	// Slight alpha falloff at the edge so the chalk outline on top reads clean.
	float alpha = smoothstep(0.0, 0.1, t) * 0.92 * u_alpha_scale;
	f_color = vec4(color, alpha);
}

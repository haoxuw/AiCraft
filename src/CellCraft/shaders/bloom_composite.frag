#version 410 core
in vec2 v_uv;
out vec4 frag;

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_strength;
uniform float u_vignette_strength;  // 0 = off, 1 = full
uniform float u_low_hp;             // 0..1 pulse strength (red edge)
uniform float u_time;

void main() {
	vec3 scene = texture(u_scene, v_uv).rgb;
	vec3 bloom = texture(u_bloom, v_uv).rgb;

	vec3 color = scene + bloom * u_bloom_strength;

	// Soft vignette.
	vec2 q = v_uv - 0.5;
	float r2 = dot(q, q);
	float vig = 1.0 - smoothstep(0.22, 0.70, r2) * u_vignette_strength;
	color *= vig;

	// Low-HP red-edge heartbeat.
	if (u_low_hp > 0.0) {
		float pulse = 0.5 + 0.5 * sin(u_time * 4.5);
		float edge  = smoothstep(0.15, 0.55, r2);
		color = mix(color, vec3(0.9, 0.1, 0.1),
		            u_low_hp * edge * (0.35 + 0.35 * pulse));
	}

	frag = vec4(color, 1.0);
}

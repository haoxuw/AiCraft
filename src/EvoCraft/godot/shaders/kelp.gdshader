shader_type spatial;
render_mode diffuse_lambert, specular_disabled, cull_disabled;

// Vertex-wave kelp. INSTANCE_CUSTOM.x = per-instance phase seed so every
// strand sways out of sync. Sway intensity grows from base (anchored) to
// tip (free). Sample INSTANCE_CUSTOM.y as optional height scalar later.

uniform vec3 base_color : source_color = vec3(0.18, 0.38, 0.28);
uniform vec3 tip_color : source_color = vec3(0.35, 0.65, 0.48);
// VERTEX.y is normalized [0,1] (local blade mesh); scale applies world height.
// Per-vertex displacement in local space gets amplified by the instance's
// Y/X scale, so sway_amount ≈ 0.15 gives a tip sweep of ~1 world unit for a
// strand ~6 world units tall.
uniform float sway_amount = 0.15;
uniform float sway_speed = 0.9;
uniform float sway_freq = 3.5;

void vertex() {
	float h = clamp(VERTEX.y, 0.0, 1.0);
	float seed = INSTANCE_CUSTOM.x;
	float t = TIME * sway_speed + seed;
	float sx = sin(t + h * sway_freq) + 0.5 * sin(t * 1.7 + h * sway_freq * 1.5);
	float sz = cos(t * 0.8 + h * sway_freq) + 0.4 * cos(t * 1.3 - h * sway_freq);
	float k = pow(h, 1.6) * sway_amount;
	VERTEX.x += sx * k;
	VERTEX.z += sz * k;
}

void fragment() {
	float h = clamp(UV.y, 0.0, 1.0);
	vec3 col = mix(base_color, tip_color, h);
	ALBEDO = col * 0.4;
	// Bioluminescent tip glow — ramps up sharply on the upper third of the
	// strand. Pulses gently so the scene feels alive at depth.
	float glow = smoothstep(0.55, 1.0, h);
	float pulse = 0.75 + 0.25 * sin(TIME * 1.6 + h * 4.0);
	EMISSION = tip_color * glow * pulse * 3.0;
	ROUGHNESS = 0.85;
	METALLIC = 0.0;
}

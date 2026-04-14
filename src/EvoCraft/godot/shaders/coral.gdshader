shader_type spatial;
render_mode specular_disabled, cull_disabled;

// Bioluminescent coral fan. Per-instance custom data:
//   .rgb   glow color (cyan / magenta / violet / lime / amber)
//   .a     phase seed (drives sway + pulse so each colony breathes
//          independently)
//
// UV.y = 0 at the base (anchored to the sea floor) → 1 at the tip.
// Albedo stays nearly black so the fan reads as a glowing silhouette in
// the abyssal dark; emission ramps up the height with a slow pulse.

uniform vec3 base_color : source_color = vec3(0.01, 0.02, 0.03);
uniform float sway_amp = 0.06;
uniform float sway_speed = 0.5;

varying float v_h;
varying vec3  v_glow;
varying float v_seed;

void vertex() {
	v_h = clamp(UV.y, 0.0, 1.0);
	v_glow = INSTANCE_CUSTOM.rgb;
	v_seed = INSTANCE_CUSTOM.a;

	// Slow, organic sway — coral fans drift in current much slower than kelp.
	float t = TIME * sway_speed + v_seed;
	float sx = sin(t + v_h * 2.5) * sway_amp * v_h;
	float sz = cos(t * 0.7 + v_h * 2.0) * sway_amp * 0.6 * v_h;
	VERTEX.x += sx;
	VERTEX.z += sz;
}

void fragment() {
	float pulse = 0.7 + 0.3 * sin(TIME * 1.4 + v_seed * 1.7);
	// Tip-weighted emission — base stays dim, polyps near the tip glow hard.
	float em = pow(v_h, 1.2) * pulse;
	ALBEDO = mix(base_color, v_glow * 0.15, v_h);
	EMISSION = v_glow * em * 4.5;
	ROUGHNESS = 0.7;
	METALLIC = 0.0;
}

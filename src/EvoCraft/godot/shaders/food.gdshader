shader_type spatial;
render_mode unshaded, specular_disabled, cull_disabled;

// Tiny glowing food pellet. Pulses in brightness so the pellets are easy
// to spot against the caustic floor.

uniform vec3 core_color : source_color = vec3(1.0, 0.9, 0.45);
uniform vec3 rim_color  : source_color = vec3(1.0, 0.5, 0.2);
uniform float pulse_speed = 2.5;

void fragment() {
	float rim = pow(1.0 - abs(dot(NORMAL, VIEW)), 2.5);
	float pulse = 0.75 + 0.25 * sin(TIME * pulse_speed);
	vec3 col = mix(core_color, rim_color, rim) * pulse;
	ALBEDO = col;
	EMISSION = col * 0.8;
}

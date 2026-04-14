shader_type spatial;
render_mode diffuse_lambert, specular_disabled, depth_draw_opaque;

// "Primordial pond" floor — Spore Cell Stage backdrop. Clear, sunlit
// turquoise water seen from above. Smooth gradient from light center to
// slightly deeper rim, gentle scrolling caustic light + soft drifting
// god-ray bands. No particle motes — those read as snow, not life.

uniform vec3  water_shallow : source_color = vec3(0.42, 0.88, 0.92);
uniform vec3  water_deep    : source_color = vec3(0.10, 0.50, 0.66);
uniform vec3  caustic_tint  : source_color = vec3(0.85, 1.00, 0.98);
uniform vec3  ray_tint      : source_color = vec3(0.78, 1.00, 0.98);
uniform float caustic_amp   = 0.30;
uniform float ray_amp       = 0.18;

void fragment() {
	float t = TIME * 0.30;
	vec2 uv = UV;

	// Smooth radial gradient — bright at center, gently darker at rim.
	// Smoothstep keeps it feeling like clear water rather than a vignette.
	vec2 cuv = uv - vec2(0.5);
	float radial = 1.0 - smoothstep(0.10, 0.75, length(cuv));
	vec3 col = mix(water_deep, water_shallow, radial);

	// Wide soft caustic lattice — large low-frequency scrolling sines.
	// Low amplitude, smooth pow curve = no flicker, just slow shimmering.
	vec2 cuv2 = uv * 4.5;
	float c1 = sin(cuv2.x * 1.0 + cuv2.y * 0.5 + t * 1.1);
	float c2 = sin(cuv2.x * 0.6 - cuv2.y * 1.0 - t * 0.9);
	float caustic = max(c1 + c2, 0.0) * 0.5;
	caustic = pow(caustic, 2.5) * caustic_amp;
	col += caustic_tint * caustic;

	// Soft vertical god-ray bands — wide, slow, and tinted. No high-frequency
	// noise; just a couple of low-freq cosines that slowly slide across the
	// dish so the water feels lit from above.
	float ray_a = cos(uv.x *  6.0 + sin(uv.y * 1.4 + t * 0.4) * 0.8 + t * 0.10);
	float ray_b = cos(uv.x * 10.0 - sin(uv.y * 1.8 - t * 0.3) * 0.6 - t * 0.16);
	float rays  = max(ray_a, 0.0) * 0.6 + max(ray_b, 0.0) * 0.4;
	rays = smoothstep(0.0, 1.0, rays) * ray_amp;
	col += ray_tint * rays;

	ALBEDO    = col;
	ROUGHNESS = 0.85;
	METALLIC  = 0.0;
}

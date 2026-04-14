shader_type spatial;
render_mode blend_mix, depth_draw_opaque, diffuse_lambert, specular_disabled, cull_disabled;

// Spore-style cartoony microbe. Bright saturated body, big white eye on
// the front, dark nucleus opposite the eye. Cilia and flagella verts are
// tagged via UV2 so the vertex shader can animate them independently.
//
// UV2 tagging convention (set in world_builder.gd mesh builders):
//   UV2.x = 0  body            — wobble around center, translucent membrane
//   UV2.x = 1  cilium / leg    — tangential wave at the tip (UV2.y = base..tip)
//   UV2.x = 2  flagellum strip — sine wave along the trailing tail
//   UV2.x = 3  nucleus         — inner opaque organelle, no wobble
//
// Per-instance custom data:
//   .x  phase seed (random per cell so wobble + pulse desync)

uniform vec3  cyto_color   : source_color = vec3(0.35, 0.85, 0.45);
uniform vec3  mem_color    : source_color = vec3(0.55, 1.00, 0.65);
uniform vec3  nuc_color    : source_color = vec3(0.10, 0.28, 0.16);
uniform float wobble_amp   = 0.04;

// Eye — set per species. eye_strength=0 disables the eye for species
// that look better without one (diatoms, radiolarians).
uniform float eye_strength = 1.0;
uniform vec2  eye_offset   = vec2(0.40, 0.0);
uniform float eye_size     = 0.20;

varying vec2  v_local;
varying float v_seed;
varying float v_kind;
varying float v_along;

void vertex() {
	v_seed  = INSTANCE_CUSTOM.x;
	v_kind  = UV2.x;
	v_along = UV2.y;

	if (v_kind > 2.5) {
		// Nucleus — static; inherits breathing scale via a separate code path
		// below (skipped here so it stays stable relative to body interior).
		float tb = TIME * 1.4 + v_seed * 0.7;
		float breathe = 1.0 + 0.025 * sin(tb);
		VERTEX *= breathe;
	} else if (v_kind > 1.5) {
		// Flagellum strip — sine wave on Z, amplitude grows toward the tip.
		// Also lifts the tail tip slightly in Y so the wave looks 3D from a
		// tilted camera, not like a flat ribbon sliding on the floor.
		float t = TIME * 8.0 + v_seed;
		float pos = clamp(-VERTEX.x / 2.8, 0.0, 1.0);
		float wave = sin(t - pos * 12.0);
		VERTEX.z += wave * 0.28 * pos;
		VERTEX.y += (cos(t - pos * 12.0) * 0.5 + 0.5) * 0.06 * pos;
	} else if (v_kind > 0.5) {
		// Cilium / leg — tangential sway at the tip, plus small Y bounce so
		// tips rock up and down visibly from the perch camera angle.
		float ang = atan(VERTEX.z, VERTEX.x);
		float t = TIME * 5.0 + v_seed * 5.0 + ang * 2.0;
		float wave = sin(t) * 0.14 * v_along;
		VERTEX.x += -sin(ang) * wave;
		VERTEX.z +=  cos(ang) * wave;
		VERTEX.y += (cos(t) * 0.5 + 0.5) * 0.05 * v_along;
	} else {
		// Body — breathe (uniform scale pulse) + equator wobble. Breathing is
		// applied as a radial scale from the dome origin so the whole blob
		// inflates/deflates instead of just its silhouette jittering.
		float tb = TIME * 1.4 + v_seed * 0.7;
		float breathe = 1.0 + 0.035 * sin(tb);
		VERTEX *= breathe;

		float r = length(VERTEX.xz);
		if (r > 0.05) {
			float ang = atan(VERTEX.z, VERTEX.x);
			float t = TIME * 1.5 + v_seed;
			float w = sin(t + ang * 4.0) * 0.5
			        + sin(t * 1.3 + ang * 2.8) * 0.5;
			float k = wobble_amp * smoothstep(0.0, 0.3, r);
			VERTEX.x += cos(ang) * w * k * r;
			VERTEX.z += sin(ang) * w * k * r;
		}
	}
	v_local = VERTEX.xz;
}

void fragment() {
	vec3 col;
	float em;
	float alpha = 1.0;

	if (v_kind > 2.5) {
		// Nucleus — soft candy-colored inner organelle. Slight self-emission
		// so it glows gently through the jelly membrane instead of looking
		// like a dark blob of bacteria.
		col = nuc_color;
		em  = 0.20;
		alpha = 1.0;
	} else if (v_kind > 0.5) {
		// Appendage — solid mem color, slight pulse so it reads as alive.
		float pulse = 0.85 + 0.15 * sin(TIME * 3.0 + v_seed);
		col = mem_color * pulse;
		em  = 0.30;
	} else {
		// Body cytoplasm with a fake top-light gradient for cartoon depth.
		vec3 base = cyto_color;
		float light = clamp(
			dot(normalize(vec2(-0.6, 0.7)),
			    normalize(v_local + vec2(0.001))) * 0.30 + 0.85,
			0.65, 1.10);
		base *= light;

		// (A real 3D nucleus mesh is drawn inside the body — no more painted
		// fake nucleus on the surface; would compete with the real one.)

		// Big cartoony eye on the front (+X). Sclera + pupil + highlight.
		if (eye_strength > 0.5) {
			vec2 e  = v_local - eye_offset;
			float ed = length(e);
			float white   = smoothstep(eye_size, eye_size * 0.88, ed);
			float pupil   = smoothstep(eye_size * 0.50,
			                            eye_size * 0.36, ed);
			vec2  hl_off  = vec2(-eye_size * 0.22, eye_size * 0.22);
			float hl      = smoothstep(eye_size * 0.20, eye_size * 0.10,
			                            length(e + hl_off));
			base = mix(base, vec3(0.97, 0.98, 0.96), white);
			base = mix(base, vec3(0.05, 0.05, 0.10), pupil);
			base = mix(base, vec3(1.0), hl * 0.85);
		}

		col = base;
		em  = 0.08;

		// Translucent membrane — fresnel so edges stay visible and the
		// middle lets the nucleus and backside show through. View-space
		// normal is cheap: we don't have NORMAL here post-vertex, so
		// approximate rim from v_local radial distance (center ≈ thin,
		// edge ≈ rim-on → glass-like silhouette).
		// Jelly body: center is ~55% opaque so the soft-colored nucleus
		// peeks through like a gummy-bear inclusion; edge is ~92% so the
		// silhouette stays crisp and cute.
		float rim = clamp(length(v_local) * 2.2, 0.0, 1.0);
		alpha = mix(0.55, 0.92, pow(rim, 1.4));
	}

	ALBEDO    = col;
	ALPHA     = alpha;
	// Nucleus self-emits in its own color; body + appendages emit in mem tint.
	EMISSION  = (v_kind > 2.5 ? nuc_color : mem_color) * em;
	ROUGHNESS = 0.55;
	METALLIC  = 0.0;
}

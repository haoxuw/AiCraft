shader_type spatial;
render_mode specular_disabled, cull_disabled;

// Full-body fish shader — body axis is +X (head) to -X (tail).
//
// Geometry tagging via UV2:
//   UV2.x ~ 0.0  body
//   UV2.x ~ 0.3  tail fin
//   UV2.x ~ 0.6  dorsal / anal fin
//   UV2.x ~ 0.8  pectoral fin
//   UV2.x ~ 1.0  eye (UV2.y > 0.5 → pupil, else iris-white)
//
// Per-instance state in INSTANCE_CUSTOM:
//   .x  phase seed (so each fish is out of sync)
//   .y  species id  (0=pike, 1=puffer, 2=angelfish — drives swim style + pattern)
//   .z  scale jitter (already baked into the instance transform; just here for ref)
//
// Swim styles by species (per icthyology refs):
//   * Pike (sub-carangiform): stiff front half, big tail amplitude — burst hunter.
//   * Puffer (ostraciiform): nearly rigid body, fast pectoral paddling, tiny tail flick.
//   * Angelfish (amiiform): body almost still, undulation propagates along the dorsal &
//     anal fins instead of the spine.

uniform float wiggle_amp   = 0.35;
uniform float wiggle_freq  = 5.0;
uniform float wiggle_speed = 9.0;
uniform float body_half_length = 0.8;

uniform vec3  color_back    : source_color = vec3(0.20, 0.55, 0.85);
uniform vec3  color_belly   : source_color = vec3(0.95, 0.95, 0.90);
uniform vec3  color_stripe  : source_color = vec3(0.05, 0.20, 0.30);
uniform vec3  color_pattern : source_color = vec3(0.05, 0.05, 0.05);
uniform float stripe_width = 0.08;

varying float v_kind;       // 0=body 1=tail 2=dorsal 3=pect 4=iris 5=pupil
varying float v_back_t;     // 0=belly, 1=back
varying float v_species;
varying vec3  v_local;

void vertex() {
	float kind_raw = UV2.x;
	bool is_eye   = kind_raw > 0.95;
	bool is_pupil = is_eye && UV2.y > 0.5;
	bool is_tail  = !is_eye && kind_raw > 0.15 && kind_raw < 0.45;
	bool is_dor   = !is_eye && kind_raw > 0.45 && kind_raw < 0.70;
	bool is_pec   = !is_eye && kind_raw > 0.70 && kind_raw < 0.95;
	bool is_fin   = is_tail || is_dor || is_pec;

	float kid;
	if (is_pupil)     kid = 5.0;
	else if (is_eye)  kid = 4.0;
	else if (is_pec)  kid = 3.0;
	else if (is_dor)  kid = 2.0;
	else if (is_tail) kid = 1.0;
	else              kid = 0.0;
	v_kind = kid;
	v_local = VERTEX;
	v_back_t = smoothstep(-0.4, 0.4, VERTEX.y);
	v_species = INSTANCE_CUSTOM.y;

	float head_t = clamp(0.5 - VERTEX.x / (body_half_length * 2.0), 0.0, 1.0);
	float seed = INSTANCE_CUSTOM.x;
	float t = TIME * wiggle_speed + seed;

	// ---- per-species body yaw -------------------------------------------------
	float yaw = 0.0;
	float species = INSTANCE_CUSTOM.y;

	if (species < 0.5) {
		// Pike: stiff front, lash-tail back. front_clip kills any motion ahead
		// of midbody so eyes/jaw stay locked while tail beats hard.
		float lag = VERTEX.x / body_half_length * (wiggle_freq * 0.65);
		float front_clip = smoothstep(0.40, 0.92, head_t);
		yaw = sin(t - lag) * front_clip * wiggle_amp * 1.4;
	} else if (species < 1.5) {
		// Puffer: barely any body bend. Just a lazy tail flick.
		float clip = smoothstep(0.78, 1.0, head_t);
		yaw = sin(t * 0.9) * clip * wiggle_amp * 0.35;
	} else {
		// Angelfish: body almost still — locomotion is in the dorsal/anal fins.
		yaw = sin(t * 0.5) * pow(head_t, 1.6) * wiggle_amp * 0.30;
	}

	// ---- fin-specific motion --------------------------------------------------
	if (is_fin) {
		if (is_pec) {
			// Pectoral flutter — puffer flutters fastest (it's all pectoral)
			float f = (species < 1.5) ? (species < 0.5 ? 11.0 : 17.0) : 8.5;
			float a = (species < 1.5) ? (species < 0.5 ? 0.07 : 0.13) : 0.06;
			yaw += sin(TIME * f + seed * 3.1) * a;
		} else if (is_tail) {
			// Tail amplifies primary wave — strongest in puffer (whole prop) less in angel.
			float k = (species < 0.5) ? 1.9 : (species < 1.5 ? 3.2 : 1.4);
			yaw *= k;
		} else if (is_dor) {
			if (species > 1.5) {
				// Angelfish amiiform: undulation runs along the dorsal/anal fin's X axis.
				float fin_lag = VERTEX.x * 6.0;
				yaw += sin(TIME * 7.0 + fin_lag + seed) * 0.22;
			} else {
				yaw *= 1.15;
			}
		}
	}

	// Eyes ride with the head — disable any local motion entirely.
	if (is_eye) {
		yaw = 0.0;
	}

	VERTEX.z += yaw;

	// ---- whole-fish vertical bob (uniform across all verts) -------------------
	VERTEX.y += sin(TIME * 1.2 + seed * 2.1) * 0.04;

	// ---- secondary body roll (slight lean into the wave) ----------------------
	float roll = sin(t * 0.55) * 0.05 * head_t;
	float cy = cos(roll), sy = sin(roll);
	float y = VERTEX.y * cy - VERTEX.z * sy;
	float z = VERTEX.y * sy + VERTEX.z * cy;
	VERTEX.y = y;
	VERTEX.z = z;
}

void fragment() {
	vec3 albedo;
	float rough;
	float spec;

	if (v_kind > 4.5) {
		// Pupil — jet black with a tiny catchlight on one side.
		float catch_ = pow(max(dot(NORMAL, vec3(0.35, 0.55, 0.75)), 0.0), 28.0);
		albedo = mix(vec3(0.015, 0.015, 0.02), vec3(1.0), catch_ * 0.95);
		rough = 0.12;
		spec  = 0.85;
	} else if (v_kind > 3.5) {
		// Iris white — slight cream tint, glossy.
		albedo = vec3(0.97, 0.96, 0.92);
		rough = 0.22;
		spec  = 0.7;
	} else {
		// Body (and fin) shading.
		vec3 base = mix(color_belly, color_back, v_back_t);

		float stripe = 1.0 - smoothstep(0.0, stripe_width,
			abs(v_back_t - 0.5));
		base = mix(base, color_stripe, stripe * 0.55);

		float pat = 0.0;
		if (v_species < 0.5) {
			// Pike — chevron/dappled bands on the back. Combine a fine spot
			// noise with horizontal banding for the classic pike camo look.
			vec2 p = v_local.xy * vec2(5.5, 8.5);
			vec2 ip = floor(p);
			vec2 fp = fract(p) - 0.5;
			float r = length(fp) - 0.20;
			float h = fract(sin(dot(ip, vec2(12.9898, 78.233)))
				* 43758.5453);
			float spot = (h > 0.50) ? smoothstep(0.05, -0.05, r) : 0.0;
			float bands = smoothstep(0.55, 0.95,
				abs(sin(v_local.x * 5.5)));
			pat = max(spot, bands * 0.55)
				* smoothstep(0.30, 0.85, v_back_t);
		} else if (v_species < 1.5) {
			// Bigger, fewer, bolder spots so they read at game-camera distance.
			vec2 p = v_local.xy * vec2(4.5, 5.5);
			vec2 ip = floor(p);
			vec2 fp = fract(p) - 0.5;
			float r = length(fp) - 0.30;
			float h = fract(sin(dot(ip, vec2(45.13, 91.7))) * 23456.789);
			float spot = (h > 0.45) ? smoothstep(0.05, -0.05, r) : 0.0;
			pat = spot * mix(0.55, 1.0, v_back_t);
		} else {
			float x = v_local.x / body_half_length;
			float bar = smoothstep(0.78, 0.95, abs(sin(x * 4.2 + 0.6)));
			pat = bar * 0.92;
		}
		base = mix(base, color_pattern, pat);

		if (v_kind > 0.5 && v_kind < 3.5) {
			base = mix(base, color_pattern, 0.18);
			base *= 0.85;
		}

		// Subtle cool back highlight — keep it understated so it doesn't look
		// like sunburn.
		float shimmer = pow(max(0.0, v_back_t - 0.7), 1.4) *
			(0.5 + 0.5 * sin(v_local.x * 18.0 + TIME * 1.6));
		base += vec3(shimmer * 0.015, shimmer * 0.02, shimmer * 0.035);

		albedo = base;
		rough = 0.45;
		spec  = 0.55;
	}

	ALBEDO = albedo;
	ROUGHNESS = rough;
	METALLIC = 0.0;
	SPECULAR = spec;
}

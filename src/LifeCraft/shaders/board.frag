#version 410 core

// Chalkboard background.
//
// Layered procedural noise — no texture asset — tuned for:
//   • high apparent resolution: 4 octaves of value noise at increasing freq
//   • "cloudy" motion: each octave drifts slowly in a different direction
//     so the surface feels alive, like a board seen through faint haze.
//     Motion is subtle enough that a still screenshot still reads as a
//     plausible board, but a real-time view has quiet atmosphere.
//
// Composition (back to front):
//   1. base slate color
//   2. low-freq dust clouds drifting horizontally  (slow, big scale)
//   3. mid-freq grain drifting in opposite direction (medium)
//   4. fine specks — stationary (board tooth doesn't move)
//   5. very low-freq eraser streaks (horizontal bands, slight drift)
//   6. vignette

in vec2 v_uv;
out vec4 f_color;

uniform vec2  u_resolution;
uniform float u_time;        // seconds since start

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float a = hash12(i);
	float b = hash12(i + vec2(1, 0));
	float c = hash12(i + vec2(0, 1));
	float d = hash12(i + vec2(1, 1));
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractional Brownian motion — summed octaves of value noise.
float fbm(vec2 p) {
	float v = 0.0;
	float amp = 0.5;
	for (int i = 0; i < 4; ++i) {
		v += amp * vnoise(p);
		p *= 2.03;      // slight non-integer so ringing doesn't align
		amp *= 0.52;
	}
	return v;
}

void main() {
	vec2 pix = gl_FragCoord.xy;

	// ---- base slate ---------------------------------------------------
	vec3 base = vec3(0.10, 0.155, 0.135);

	// ---- dust clouds: big, slow, mostly horizontal drift -------------
	// Chalk dust suspended just above the board — broad swells that
	// lighten / darken regions visibly over a few seconds. Drift speed
	// is in noise-space units/sec; at pix*0.0025 scale, one noise unit
	// is ~400 pixels, so 0.08/s ≈ 32 pixels/sec — clearly visible drift.
	vec2  cloudUV  = pix * 0.0025 + vec2(u_time * 0.08, u_time * 0.03);
	float clouds   = fbm(cloudUV);
	base += (clouds - 0.5) * 0.12;

	// ---- mid-freq grain drifting the other way ----------------------
	// Faster and a bit stronger so the surface "breathes" rather than
	// just sits there.
	vec2  grainUV = pix * 0.05 + vec2(-u_time * 0.25, u_time * 0.12);
	float grain   = fbm(grainUV);
	base += (grain - 0.5) * 0.08;

	// ---- fine stationary specks -------------------------------------
	// Actual board texture / tooth — this is physical, doesn't move.
	float specks = hash12(floor(pix * 1.5));
	base += (specks - 0.5) * 0.018;

	// ---- eraser streaks ---------------------------------------------
	// Horizontal bands where someone swept a chalk eraser. Drift
	// horizontally — slow enough to feel atmospheric but clearly moving.
	vec2  streakUV = vec2(pix.x * 0.003 - u_time * 0.15, pix.y * 0.018);
	float streak   = fbm(streakUV);
	base += smoothstep(0.55, 0.82, streak) * 0.09;

	// Occasional brighter smudges where chalk was rubbed but not fully
	// cleared — slow swirl through the scene.
	vec2  smudgeUV = pix * 0.006 + vec2(u_time * 0.05, -u_time * 0.02);
	float smudge   = fbm(smudgeUV + vec2(31.7, 12.3));
	base += smoothstep(0.72, 0.92, smudge) * 0.10;

	// ---- vignette ---------------------------------------------------
	vec2 nd = (pix / u_resolution) * 2.0 - 1.0;
	float vig = 1.0 - dot(nd, nd) * 0.22;
	base *= clamp(vig, 0.0, 1.0);

	f_color = vec4(base, 1.0);
}

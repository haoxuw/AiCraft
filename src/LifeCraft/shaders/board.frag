#version 410 core

// Chalkboard background — layered procedural noise, no texture asset.
//
// Look target: modern, crisp, high-definition chalkboard. High-freq
// detail visible at pixel scale, broad cloud layers drifting overhead
// at different rates for parallax depth. The rule: every octave should
// *add* detail, never just wash out what's underneath.

in vec2 v_uv;
out vec4 f_color;

uniform vec2  u_resolution;
uniform float u_time;

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

// 7-octave FBM — broad shape down to ~2px detail. Each octave doubles
// freq, amplitude falls off so the finest octaves are a thin crisp
// overlay rather than washing out the broad shapes.
float fbm(vec2 p) {
	float v = 0.0;
	float amp = 0.5;
	for (int i = 0; i < 7; ++i) {
		v += amp * vnoise(p);
		p *= 2.11;
		amp *= 0.55;
	}
	return v;
}

// Contrast sharpen around 0.5 — pushes light peaks lighter and dark
// troughs darker so cloud fronts read as distinct, not uniform haze.
float sharpen(float v, float k) {
	float c = (v - 0.5) * k;
	return 0.5 + 0.5 * c / (1.0 + abs(c));
}

void main() {
	vec2 pix = gl_FragCoord.xy;

	// ---- base slate ---------------------------------------------------
	vec3 base = vec3(0.095, 0.150, 0.130);

	// ---- large parallax cloud (slow, big) -----------------------------
	// Huge features covering a substantial fraction of the screen.
	// Low frequency, slow drift — feels like atmosphere in the background.
	vec2  cloudA_uv = pix * 0.0009 + vec2(-u_time * 0.09, u_time * 0.04);
	float cloudA    = sharpen(fbm(cloudA_uv), 2.0);
	base += (cloudA - 0.5) * 0.13;

	// ---- main cloud layer (medium scale, faster) ---------------------
	// This is the "weather" — distinct brighter/darker fronts that
	// visibly drift. Sharpened so the fronts are crisp, not smeared.
	vec2  cloudB_uv = pix * 0.0026 + vec2(u_time * 0.32, u_time * 0.11);
	float cloudB    = sharpen(fbm(cloudB_uv), 2.6);
	base += (cloudB - 0.5) * 0.22;

	// ---- mid-freq grain (drifting counter to cloudB) ------------------
	vec2  grain_uv = pix * 0.028 + vec2(-u_time * 0.45, u_time * 0.22);
	float grain    = fbm(grain_uv);
	base += (grain - 0.5) * 0.09;

	// ---- fine pixel-scale tooth --------------------------------------
	// The stuff that makes the board LOOK high-resolution: small, sharp,
	// slightly random per-pixel variation. No interpolation — one hash
	// sample per ~2px. This is the crisp detail you notice as "this
	// isn't an upscaled image".
	float tooth   = hash12(floor(pix * 0.5));
	float tooth2  = hash12(floor(pix * 1.0) + 17.0);
	base += (tooth  - 0.5) * 0.025;
	base += (tooth2 - 0.5) * 0.018;

	// ---- eraser streaks (broad bright horizontal sweeps) -------------
	vec2  streak_uv = vec2(pix.x * 0.0025 - u_time * 0.45, pix.y * 0.016);
	float streak    = fbm(streak_uv);
	base += smoothstep(0.54, 0.82, streak) * 0.11;

	// ---- vignette ----------------------------------------------------
	vec2 nd = (pix / u_resolution) * 2.0 - 1.0;
	float vig = 1.0 - dot(nd, nd) * 0.20;
	base *= clamp(vig, 0.0, 1.0);

	f_color = vec4(base, 1.0);
}

#version 450

// Procedural sky: zenith/horizon gradient, sun corona, sunrise/sunset bleed,
// stars (at night), moon (opposite the sun), and wind-driven cloud band.
// Driven entirely by sunDir + sunStrength + a time phase — server owns the
// time, client owns the visuals (CivCraft Rule 3 + Rule 5).

layout(location = 0) in vec3 vRayDir;

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir;    // xyz, w = sunStrength (0 night … 1 full day)
	vec4 skyParams; // x = timeSec, yzw reserved
} pc;

layout(location = 0) out vec4 outColor;

// 3D hash in the [0..1)³ range — used to tile the starfield.
float hash3(vec3 p) {
	return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

// 2D gradient noise for the cloud layer. Hashes two corners, mixes with a
// smoothstep curve — cheap, seamless, and not tied to any texture.
vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return fract(sin(p) * 43758.5453) * 2.0 - 1.0;
}

float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = dot(hash2(i + vec2(0,0)), f - vec2(0,0));
	float b = dot(hash2(i + vec2(1,0)), f - vec2(1,0));
	float c = dot(hash2(i + vec2(0,1)), f - vec2(0,1));
	float d = dot(hash2(i + vec2(1,1)), f - vec2(1,1));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.5 + 0.5;
}

// 5-octave fBm for cumulus-looking clumps.
float fbm(vec2 p) {
	float v = 0.0, amp = 0.5;
	for (int i = 0; i < 5; i++) {
		v += amp * vnoise(p);
		p  = p * 2.02 + vec2(7.1, 3.7);
		amp *= 0.5;
	}
	return v;
}

void main() {
	vec3 dir     = normalize(vRayDir);
	vec3 sun     = normalize(pc.sunDir.xyz);
	float sunStr = pc.sunDir.w;
	float time   = pc.skyParams.x;

	// ── Palette LUT ────────────────────────────────────────────────────
	// Four anchors: deep night, twilight, dawn/dusk bleed, full day.
	// Blended by sunStr so the gradient tracks server worldTime exactly.
	// Day anchors tuned to Mojang canonical sky #7BA4FF (0.482, 0.643, 1.000).
	// Zenith is a punchier blue to read clearly against terrain; horizon sits
	// at the canonical value so the sky fades toward what MC players expect.
	vec3 zenithNight   = vec3(0.004, 0.008, 0.038);   // near-black indigo
	vec3 horizonNight  = vec3(0.018, 0.028, 0.078);   // muted navy
	vec3 zenithDawn    = vec3(0.090, 0.120, 0.300);   // cool pre-dawn
	vec3 horizonDawn   = vec3(0.880, 0.430, 0.260);   // warm peach band
	vec3 zenithDay     = vec3(0.180, 0.400, 0.860);   // saturated noon blue (above MC canonical)
	vec3 horizonDay    = vec3(0.482, 0.643, 1.000);   // Mojang canonical sky #7BA4FF

	// sunStr drives two stages: night→dawn up to ~0.25, dawn→day up to ~0.75.
	// The "dawnBlend" hump gives the peach horizon at low-sun angles.
	float dayBlend  = smoothstep(0.20, 0.75, sunStr);
	float dawnBlend = smoothstep(0.00, 0.35, sunStr) * (1.0 - dayBlend);
	float nightBlend = 1.0 - smoothstep(0.00, 0.30, sunStr);

	vec3 zenith  = zenithNight  * nightBlend
	             + zenithDawn   * dawnBlend
	             + zenithDay    * dayBlend;
	vec3 horizon = horizonNight * nightBlend
	             + horizonDawn  * dawnBlend
	             + horizonDay   * dayBlend;

	// ── Gradient ───────────────────────────────────────────────────────
	float t   = pow(max(dir.y, 0.0), 0.35);
	vec3  sky = mix(horizon, zenith, t);

	// Below-horizon: darken to a ground tint (prevents a hard seam with
	// the distance fog when looking down).
	if (dir.y < 0.0) {
		vec3 ground = horizon * vec3(0.45, 0.45, 0.42);
		sky = mix(horizon, ground, min(-dir.y * 5.0, 1.0));
	}

	// ── Sunrise / sunset horizon bleed ─────────────────────────────────
	// When sun is low (|sunDir.y| small) but present (sunStr > 0), paint a
	// warm band across the horizon where ray and sun share azimuth.
	float horizProximity = 1.0 - smoothstep(0.0, 0.30, abs(sun.y));
	vec2 sunXZ = normalize(vec2(sun.x, sun.z) + 1e-4);
	vec2 dirXZ = normalize(vec2(dir.x, dir.z) + 1e-4);
	float azimAlign = max(dot(sunXZ, dirXZ), 0.0);
	float horizBand = pow(max(1.0 - abs(dir.y), 0.0), 3.5);
	vec3 sunsetTint = vec3(1.00, 0.55, 0.25);
	sky += sunsetTint * horizBand * azimAlign * horizProximity
	                  * sunStr * (1.0 - sunStr * 0.5) * 1.2;

	// ── Sun disc + corona ──────────────────────────────────────────────
	float sunDot = max(dot(dir, sun), 0.0);
	sky += horizon * pow(sunDot,  3.0) * 0.22 * sunStr;
	sky += vec3(1.00, 0.84, 0.52) * pow(sunDot,  6.0) * 0.45 * sunStr;
	sky += vec3(1.00, 0.92, 0.70) * pow(sunDot, 32.0) * 1.2  * sunStr;
	sky += vec3(1.00, 0.98, 0.88) * pow(sunDot, 512.0) * 6.0 * sunStr;

	// ── Moon disc + soft halo (opposite hemisphere of the sun) ─────────
	// Positioned at −sun so it rises as the sun sets.
	vec3 moon     = -sun;
	float moonDot = max(dot(dir, moon), 0.0);
	float moonVis = 1.0 - dayBlend;  // fade out as the sun climbs
	// Cold white disc + subtle halo.
	vec3 moonColor = vec3(0.90, 0.92, 1.00);
	sky += moonColor * pow(moonDot, 128.0) * 3.2 * moonVis;    // disc
	sky += moonColor * pow(moonDot,  12.0) * 0.14 * moonVis;   // halo

	// ── Stars ──────────────────────────────────────────────────────────
	// Quantize the ray to a tight grid; spawn a star only where the hash
	// spikes past a threshold. Twinkle via a slow sin on the grid cell.
	if (dir.y > -0.05) {
		vec3 sgrid = dir * 220.0;
		vec3 ic    = floor(sgrid);
		float h    = hash3(ic);
		float starField = smoothstep(0.996, 1.000, h);
		float twinkle   = 0.6 + 0.4 * sin(time * 2.0 + h * 47.0);
		// Stars fade out at dawn and below the horizon.
		float starVis = smoothstep(0.0, 0.40, -sunStr + 0.40) * clamp(dir.y + 0.05, 0.0, 1.0);
		sky += vec3(0.95, 0.95, 1.00) * starField * twinkle * starVis * 1.6;
	}

	// ── Cumulus cloud layer ────────────────────────────────────────────
	// Project the view ray onto a virtual cloud plane at altitude 1 and
	// sample fBm there. Domain-warping the sample point gives ragged puff
	// shapes instead of smooth blobs. Coverage/softness thresholds carve
	// out clear sky between the clouds (no continuous "flame" veil).
	if (dir.y > 0.02) {
		// Sample noise on the view-direction sphere, not a projected plane —
		// plane projection explodes near the horizon (dir.y → 0) and kills
		// spatial coherence. Dir.xz ∈ [-1,1] scaled to a few features.
		vec2 cp      = vec2(dir.x, dir.z) * 5.0;
		float wind   = time * 0.015;
		vec2 warp    = vec2(
			fbm(cp * 0.6 + vec2(wind, wind * 0.3)),
			fbm(cp * 0.6 + vec2(-wind * 0.5, wind))) - 0.5;
		float n      = fbm(cp + warp * 1.4 + vec2(wind * 0.7, wind * 0.3));

		// Coverage carves puffs out of the noise field (lower = more sky).
		float coverage = 0.48;
		float softness = 0.14;
		float cloud    = smoothstep(coverage, coverage + softness, n);

		// Thin out near the horizon (atmospheric perspective).
		cloud *= smoothstep(0.05, 0.30, dir.y);

		// Base brightness: silver at night, bright white at noon. Ramps up
		// fast so morning/evening clouds still read clearly.
		float brightRamp = smoothstep(-0.10, 0.60, sunStr);
		vec3  cloudBase  = mix(vec3(0.28, 0.32, 0.42),
		                       vec3(1.00, 0.99, 0.96),
		                       brightRamp);

		// Sunrise/sunset pink: when the sun sits low, the whole cloud layer
		// catches warm light — grazing rays redden every underside. The
		// cloud on the sunward azimuth gets hotter coral; the anti-sun
		// side leans cooler mauve. Works in any camera direction.
		vec2  dirAz  = length(dir.xz) > 0.001 ? normalize(vec2(dir.x, dir.z)) : vec2(0);
		vec2  sunAz  = length(sun.xz) > 0.001 ? normalize(vec2(sun.x, sun.z)) : vec2(1, 0);
		float sunAlign = dot(dirAz, sunAz) * 0.5 + 0.5;                 // [0..1]
		// Keep the pink alive through a meaningful part of morning/evening.
		float sunLowness = 1.0 - smoothstep(0.0, 0.45, abs(sun.y));
		// Saturated coral on the sun side, mauve/violet on the opposite —
		// matches real magic-hour clouds, clearly reads as "pink sunrise".
		vec3  coralSide = vec3(1.50, 0.45, 0.38);
		vec3  mauveSide = vec3(0.70, 0.55, 0.95);
		vec3  magicHour = mix(mauveSide, coralSide, sunAlign);
		float magicStr  = sunLowness * smoothstep(0.02, 0.40, sunStr);
		cloudBase = mix(cloudBase, cloudBase * magicHour, clamp(magicStr, 0.0, 1.0));

		// Depth shade: thick puff cores slightly darker/cooler than edges.
		float depthShade = mix(0.78, 1.00, smoothstep(0.30, 1.00, n));
		vec3  cloudColor = cloudBase * depthShade;

		sky = mix(sky, cloudColor, cloud);
	}

	outColor = vec4(sky, 1.0);
}

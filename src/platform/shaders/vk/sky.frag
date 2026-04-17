#version 450

// Procedural sky. Two cloud decks on a flat projection (Minecraft/Valheim-style
// layering, not sphere-sampled puffs), a dome gradient, soft aerosol haze at
// the horizon, sun/moon disc + corona, stars, magic-hour rim on the sunward
// side of the lower deck. Driven entirely by sunDir + sunStrength + a time
// phase — server owns the time, client owns the visuals (Rule 3 + Rule 5).

layout(location = 0) in vec3 vRayDir;

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir;    // xyz, w = sunStrength (0 night … 1 full day)
	vec4 skyParams; // x = timeSec, yzw reserved
} pc;

layout(location = 0) out vec4 outColor;

float hash3(vec3 p) {
	return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

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
	// Day horizon sits at Mojang canonical #7BA4FF so the fade matches MC
	// reference; zenith is a punchier blue so sky reads against terrain.
	vec3 zenithNight   = vec3(0.004, 0.008, 0.038);
	vec3 horizonNight  = vec3(0.018, 0.028, 0.078);
	vec3 zenithDawn    = vec3(0.080, 0.110, 0.280);
	vec3 horizonDawn   = vec3(0.880, 0.430, 0.260);
	vec3 zenithDay     = vec3(0.180, 0.400, 0.860);
	vec3 horizonDay    = vec3(0.482, 0.643, 1.000);

	float dayBlend   = smoothstep(0.20, 0.75, sunStr);
	float dawnBlend  = smoothstep(0.00, 0.35, sunStr) * (1.0 - dayBlend);
	float nightBlend = 1.0 - smoothstep(0.00, 0.30, sunStr);

	vec3 zenith  = zenithNight  * nightBlend + zenithDawn  * dawnBlend + zenithDay  * dayBlend;
	vec3 horizon = horizonNight * nightBlend + horizonDawn * dawnBlend + horizonDay * dayBlend;

	// ── Gradient ───────────────────────────────────────────────────────
	// pow 0.50 gives a rounder dome than 0.35; the horizon band compresses
	// less and the zenith blue reaches farther down, closer to real skies.
	float t   = pow(max(dir.y, 0.0), 0.50);
	vec3  sky = mix(horizon, zenith, t);

	// Below-horizon: darken to a ground tint.
	if (dir.y < 0.0) {
		vec3 ground = horizon * vec3(0.45, 0.45, 0.42);
		sky = mix(horizon, ground, min(-dir.y * 5.0, 1.0));
	}

	// ── Aerosol haze band ──────────────────────────────────────────────
	// Narrow bright band hugging the horizon — the dusty/hazy layer real
	// atmospheres get near the ground. Brightens very low dir.y toward a
	// warmer version of the horizon color.
	float hazeBand = smoothstep(0.00, 0.12, dir.y) * (1.0 - smoothstep(0.12, 0.35, dir.y));
	vec3  hazeTint = mix(horizon * 1.15, vec3(1.00, 0.95, 0.85), 0.15) * (0.55 + 0.45 * sunStr);
	sky = mix(sky, hazeTint, hazeBand * 0.35);

	// ── Sunrise / sunset horizon bleed ─────────────────────────────────
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
	vec3 moon     = -sun;
	float moonDot = max(dot(dir, moon), 0.0);
	float moonVis = 1.0 - dayBlend;
	vec3 moonColor = vec3(0.90, 0.92, 1.00);
	sky += moonColor * pow(moonDot, 128.0) * 3.2 * moonVis;
	sky += moonColor * pow(moonDot,  12.0) * 0.14 * moonVis;

	// ── Stars ──────────────────────────────────────────────────────────
	if (dir.y > -0.05) {
		vec3 sgrid = dir * 220.0;
		vec3 ic    = floor(sgrid);
		float h    = hash3(ic);
		float starField = smoothstep(0.996, 1.000, h);
		float twinkle   = 0.6 + 0.4 * sin(time * 2.0 + h * 47.0);
		float starVis = smoothstep(0.0, 0.40, -sunStr + 0.40) * clamp(dir.y + 0.05, 0.0, 1.0);
		sky += vec3(0.95, 0.95, 1.00) * starField * twinkle * starVis * 1.6;
	}

	// ── Cloud decks (low cumulus + high cirrus) ────────────────────────
	// Both layers use true plane projection — the ray is intersected with a
	// flat plane at a fixed altitude, so clouds compress naturally toward
	// the horizon. This is the recognizable Minecraft/Valheim cloud-deck
	// look: low cumulus drift, high cirrus streaks far above.
	if (dir.y > 0.01) {
		vec2  sunAz    = length(sun.xz) > 0.001 ? normalize(vec2(sun.x, sun.z)) : vec2(1, 0);
		vec2  dirAz    = length(dir.xz) > 0.001 ? normalize(vec2(dir.x, dir.z)) : vec2(0);
		float sunAlign = dot(dirAz, sunAz) * 0.5 + 0.5;
		float sunLow   = 1.0 - smoothstep(0.0, 0.45, abs(sun.y));

		float brightRamp = smoothstep(-0.10, 0.60, sunStr);
		vec3  cloudLit   = mix(vec3(0.28, 0.32, 0.42),
		                       vec3(1.00, 0.99, 0.96), brightRamp);

		// Magic-hour tint — clamped so HDR values can't spike into neon.
		vec3  coralSide = vec3(1.25, 0.55, 0.42);
		vec3  mauveSide = vec3(0.75, 0.55, 0.90);
		vec3  magicHour = mix(mauveSide, coralSide, sunAlign);
		float magicStr  = sunLow * smoothstep(0.02, 0.40, sunStr) * 0.85;

		// ── Layer 1: low cumulus deck, plane at y=200. Thick, wind-driven.
		{
			float altitude = 200.0;
			float tRay     = altitude / max(dir.y, 0.001);
			vec2  cp       = vec2(dir.x, dir.z) * tRay * 0.0055;
			float wind     = time * 0.020;
			vec2  warp     = vec2(
				fbm(cp * 0.6 + vec2(wind,         wind * 0.3)),
				fbm(cp * 0.6 + vec2(-wind * 0.5,  wind      ))) - 0.5;
			float n        = fbm(cp + warp * 1.4 + vec2(wind * 0.7, wind * 0.3));

			float coverage = 0.50;
			float softness = 0.16;
			float cloud    = smoothstep(coverage, coverage + softness, n);
			// Horizon fade — keeps the deck from piling into an ugly edge.
			cloud *= smoothstep(0.02, 0.25, dir.y) * (1.0 - smoothstep(0.85, 1.00, dir.y));

			// Depth shade: thick puff cores slightly darker than edges.
			float depth     = mix(0.75, 1.00, smoothstep(0.30, 1.00, n));
			// Silver-lining rim: sample coverage gradient against the sun
			// azimuth to brighten sunward edges. Cheap approximation of
			// volumetric light leakage — gives clouds a glow outline.
			float rim       = smoothstep(coverage + softness * 0.4,
			                             coverage + softness * 1.1, n) - cloud;
			rim             = max(rim, 0.0) * (0.4 + 0.6 * sunAlign) * sunStr;
			vec3  cloudCol  = cloudLit * depth;
			cloudCol        = mix(cloudCol, cloudCol * magicHour, clamp(magicStr, 0.0, 1.0));
			cloudCol       += vec3(1.00, 0.88, 0.65) * rim * 0.55;

			sky = mix(sky, cloudCol, cloud);
		}

		// ── Layer 2: high cirrus streaks, plane at y=500. Anisotropic so
		// puffs stretch into wind-combed ribbons instead of puffs.
		{
			float altitude = 500.0;
			float tRay     = altitude / max(dir.y, 0.001);
			// Elongate along X so cirrus reads as streaks not pillows.
			vec2  cp       = vec2(dir.x * 0.35, dir.z) * tRay * 0.0020;
			float wind     = time * 0.008;
			float n        = fbm(cp + vec2(wind, 0.0));

			float coverage = 0.56;
			float softness = 0.22;
			float cirrus   = smoothstep(coverage, coverage + softness, n);
			cirrus        *= smoothstep(0.05, 0.30, dir.y);
			cirrus        *= 0.55; // thinner than cumulus

			vec3 cirrusCol = mix(vec3(0.92, 0.94, 1.00),
			                     vec3(1.00, 0.80, 0.55), magicStr * 0.6);
			sky = mix(sky, cirrusCol, cirrus);
		}
	}

	outColor = vec4(max(sky, 0.0), 1.0);
}

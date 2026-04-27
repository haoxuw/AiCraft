#version 450

// Solarium sky.
//
// 1) Two-anchor blue gradient (horizon → zenith) faded to night by sunStr.
// 2) Stylized cumulus puffs on a flat plane at altitude 200, anchored to
//    world XZ via camPos (clouds stay still as the player moves) with slow
//    wind advection driven by time. Sharp threshold + domain warp gives
//    them organic puffy shapes with discrete edges, drawn ON TOP of the
//    blue rather than blended into it. No haze band, no wide sun corona.

layout(location = 0) in vec2 vNDC;

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir;       // xyz + sunStr
	vec4 cloudParams;  // (camX, camY, camZ, time)
} pc;

layout(location = 0) out vec4 outColor;

// ── Hash + 2D value-noise FBM ──────────────────────────────────────────

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
		v   += amp * vnoise(p);
		p    = p * 2.07 + vec2(7.1, 3.7);
		amp *= 0.5;
	}
	return v;
}

void main() {
	// Reconstruct world-space ray per fragment.
	vec4 nearW = pc.invVP * vec4(vNDC, 0.0, 1.0);
	vec4 farW  = pc.invVP * vec4(vNDC, 1.0, 1.0);
	vec3 dir   = normalize(farW.xyz / farW.w - nearW.xyz / nearW.w);

	vec3  sun    = normalize(pc.sunDir.xyz);
	float sunStr = clamp(pc.sunDir.w, 0.0, 1.0);
	vec3  camPos = pc.cloudParams.xyz;
	float time   = pc.cloudParams.w;

	// ── Sky dome ───────────────────────────────────────────────────────
	vec3 zenithDay  = vec3(0.08, 0.28, 0.78);
	vec3 horizonDay = vec3(0.32, 0.55, 0.92);
	float t   = pow(max(dir.y, 0.0), 0.50);
	vec3  day = mix(horizonDay, zenithDay, t);
	vec3 night = vec3(0.020, 0.030, 0.080);
	vec3 sky = mix(night, day, sunStr);

	if (dir.y < 0.0) {
		float depth = clamp(-dir.y * 5.0, 0.0, 1.0);
		sky *= mix(1.0, 0.45, depth);
	}

	// ── Stylized cumulus puffs ─────────────────────────────────────────
	// Flat plane projection at altitude 200. Each fragment ray that points
	// upward intersects the plane at world XZ = camPos.xz + dir.xz * tRay.
	// Sampling 2D FBM at that XZ gives the cloud noise; thresholding gives
	// the puff silhouette. Anchoring to camPos.xz (not just dir.xz) means
	// clouds stay put in world space as the player walks under them.
	if (dir.y > 0.02 && sunStr > 0.05) {
		const float ALTITUDE = 200.0;
		float tRay = (ALTITUDE - camPos.y) / max(dir.y, 0.001);
		// Skip if ray goes the wrong way (camera above clouds — won't happen
		// in normal play but kept for safety).
		if (tRay > 0.0) {
			vec2 worldXZ = camPos.xz + dir.xz * tRay;
			// Slow wind drift (~5 m/s)
			vec2 wind = vec2(time * 5.0, time * 1.5);
			// Noise scale: 1 unit = ~660 blocks → puff features ~150-300 m wide
			vec2 cp = (worldXZ + wind) * 0.0015;

			// Domain warp — warps the noise sample point by lower-freq noise.
			// Without this, FBM gives too-uniform circular blobs; with it,
			// puffs get organic curved edges (the Ghibli/Genshin look).
			vec2 warp = vec2(
				fbm(cp * 0.5 + vec2(0.0)),
				fbm(cp * 0.5 + vec2(13.7))) - 0.5;
			float n = fbm(cp + warp * 0.6);

			// Crisp threshold — softness 0.04 gives drawn-on-top edges, not
			// blended-with-sky haze. Coverage 0.55 keeps lots of blue between.
			const float COVERAGE = 0.55;
			const float SOFTNESS = 0.04;
			float cloud = smoothstep(COVERAGE, COVERAGE + SOFTNESS, n);

			// Horizon clip — fade the lowest deck line so puffs don't tile
			// into a hard band at the horizon.
			cloud *= smoothstep(0.05, 0.16, dir.y);

			// Far-distance fade — clouds at extreme tRay become subpixel and
			// alias badly; fade out beyond ~8 km.
			cloud *= 1.0 - smoothstep(6000.0, 12000.0, tRay);

			if (cloud > 0.001) {
				// Internal density gradient → bright top, blue-tinted shaded bottom.
				// "Depth" is how far past threshold the noise is (cores brighter).
				float depth = smoothstep(COVERAGE, COVERAGE + 0.20, n);

				vec3 cloudLit   = vec3(0.96, 0.97, 0.99);   // bright but not pure white
				vec3 cloudShade = vec3(0.55, 0.62, 0.78);   // blue-violet underside
				vec3 cloudCol   = mix(cloudShade, cloudLit, depth);

				// Silver lining — sun-aligned edges catch a warm rim. Computed
				// as the noise gradient (puff edge) ANDed with the sun azimuth.
				vec2 sunXZ = normalize(vec2(sun.x, sun.z) + 1e-4);
				vec2 dirXZ = normalize(vec2(dir.x, dir.z) + 1e-4);
				float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
				// edge mask = 1 at the threshold, 0 in the cloud interior
				float edge = 1.0 - depth;
				cloudCol += vec3(1.00, 0.92, 0.70) * edge * sunAlign * 0.45 * sunStr;

				// Day/night fade — clouds only really exist when there's light
				// to lift them off the night sky; at deep night they sit barely
				// visible as faint silhouettes.
				cloudCol *= mix(0.20, 1.0, sunStr);

				sky = mix(sky, cloudCol, cloud);
			}
		}
	}

	outColor = vec4(max(sky, 0.0), 1.0);
}

#version 410 core

in vec3 vRayDir;

uniform vec3 uSkyColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDir;
uniform float uSunStrength; // 0..1
uniform float uTime;        // elapsed seconds (for cloud animation)

out vec4 fragColor;

// Hash for noise and star positions
float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

// 2D value noise for cloud layer
float noise2D(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	vec2 i01 = i + vec2(0, 1);
	vec2 i10 = i + vec2(1, 0);
	vec2 i11 = i + vec2(1, 1);
	return mix(
		mix(hash(vec3(i, 0)),  hash(vec3(i10, 0)),  f.x),
		mix(hash(vec3(i01, 0)), hash(vec3(i11, 0)), f.x),
		f.y);
}

void main() {
	vec3 dir = normalize(vRayDir);

	// Gradient: horizon -> zenith
	float t = max(dir.y, 0.0);
	t = pow(t, 0.4);
	vec3 sky = mix(uHorizonColor, uSkyColor, t);

	// Below horizon: ground tint
	if (dir.y < 0.0) {
		vec3 groundColor = uHorizonColor * vec3(0.75, 0.80, 0.70);
		sky = mix(uHorizonColor, groundColor, min(-dir.y * 4.0, 1.0));
	}

	// Sun glow
	float sunDot = max(dot(dir, uSunDir), 0.0);
	// Soft atmospheric halo (wide)
	sky += uHorizonColor * pow(sunDot, 4.0) * 0.12 * uSunStrength;
	// Medium glow
	sky += vec3(1.0, 0.90, 0.65) * pow(sunDot, 8.0) * 0.25 * uSunStrength;
	// Bright inner halo
	sky += vec3(1.0, 0.95, 0.80) * pow(sunDot, 64.0) * 0.8;
	// Sun disc
	sky += vec3(1.0, 1.0, 0.95) * pow(sunDot, 1024.0) * 3.0;

	// Sunset/sunrise glow on horizon
	float horizGlow = pow(max(1.0 - abs(dir.y), 0.0), 4.0);
	float sunsetFactor = (1.0 - uSunStrength) * max(uSunStrength * 4.0, 0.0);
	sky += vec3(1.0, 0.4, 0.15) * horizGlow * sunsetFactor * 0.5;

	// ── Procedural cloud layer ──
	// Only draw clouds above horizon (dir.y > threshold)
	float cloudVis = smoothstep(0.03, 0.18, dir.y);
	if (cloudVis > 0.001) {
		// Project ray onto cloud plane — gives stable XZ coords
		vec2 cloudUV = dir.xz / max(dir.y, 0.03) * 0.40;
		// Slow wind drift
		cloudUV += vec2(uTime * 0.009, uTime * 0.003);

		// Three octaves of noise for natural pufffy shape
		float cn = noise2D(cloudUV * 1.0)               * 0.50
		         + noise2D(cloudUV * 2.3 + vec2(0.5, 0.7)) * 0.30
		         + noise2D(cloudUV * 5.1 + vec2(1.2, 0.3)) * 0.20;
		cn = smoothstep(0.50, 0.70, cn);

		// Cloud color: warm pink at sunrise/sunset, white-blue in day, grey at night
		vec3 cloudLit   = mix(vec3(0.95, 0.75, 0.60),  vec3(0.96, 0.97, 1.00), uSunStrength);
		vec3 cloudShade = cloudLit * mix(0.55, 0.78, uSunStrength);
		vec3 cloudColor = mix(cloudShade, cloudLit, 0.65);

		sky = mix(sky, cloudColor, cn * cloudVis);
	}

	// Moon (opposite sun)
	vec3 moonDir = -uSunDir;
	moonDir.y = abs(moonDir.y); // keep moon above horizon
	float moonDot = max(dot(dir, moonDir), 0.0);
	float nightFactor = 1.0 - uSunStrength;
	sky += vec3(0.8, 0.85, 1.0) * pow(moonDot, 256.0) * 1.5 * nightFactor;
	sky += vec3(0.5, 0.55, 0.7) * pow(moonDot, 32.0) * 0.15 * nightFactor;

	// Stars (visible at night, above horizon)
	if (dir.y > 0.0 && uSunStrength < 0.6) {
		float starVisibility = smoothstep(0.6, 0.1, uSunStrength);
		// Grid-based star placement
		vec3 starGrid = dir * 80.0;
		vec3 cell = floor(starGrid);
		float starVal = hash(cell);
		if (starVal > 0.97) {
			// Bright star
			vec3 cellCenter = (cell + 0.5) / 80.0;
			float dist = length(dir - normalize(cellCenter));
			float brightness = smoothstep(0.008, 0.001, dist);
			float twinkle = 0.7 + 0.3 * sin(starVal * 100.0 + uTime * 1.5);
			sky += vec3(1.0, 0.95, 0.85) * brightness * twinkle * starVisibility;
		} else if (starVal > 0.93) {
			// Dim star
			vec3 cellCenter = (cell + 0.5) / 80.0;
			float dist = length(dir - normalize(cellCenter));
			float brightness = smoothstep(0.006, 0.002, dist) * 0.4;
			sky += vec3(0.7, 0.8, 1.0) * brightness * starVisibility;
		}
	}

	fragColor = vec4(sky, 1.0);
}

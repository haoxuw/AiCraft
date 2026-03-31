#version 410 core

in vec3 vRayDir;

uniform vec3 uSkyColor;
uniform vec3 uHorizonColor;
uniform vec3 uSunDir;
uniform float uSunStrength; // 0..1

out vec4 fragColor;

// Hash for star positions
float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
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
	sky += vec3(1.0, 0.90, 0.65) * pow(sunDot, 8.0) * 0.25 * uSunStrength;
	sky += vec3(1.0, 0.95, 0.80) * pow(sunDot, 64.0) * 0.8;
	sky += vec3(1.0, 1.0, 0.95) * pow(sunDot, 1024.0) * 3.0;

	// Sunset/sunrise glow on horizon
	float horizGlow = pow(max(1.0 - abs(dir.y), 0.0), 4.0);
	float sunsetFactor = (1.0 - uSunStrength) * max(uSunStrength * 4.0, 0.0);
	sky += vec3(1.0, 0.4, 0.15) * horizGlow * sunsetFactor * 0.5;

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
			float twinkle = 0.7 + 0.3 * sin(starVal * 100.0 + uSunStrength * 20.0);
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

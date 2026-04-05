#version 410 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
in float vShade;
in float vAlpha;

uniform vec3 uSunDir;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uSunStrength; // 0..1, 0=night, 1=day
uniform float uTime;        // elapsed seconds (for animations)

out vec4 fragColor;

// Hash function for procedural noise
float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

// Value noise
float noise3D(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f); // smoothstep

	return mix(
		mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
		    mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
		mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
		    mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
		f.z
	);
}

void main() {
	// Per-block position (integer part = block ID for consistent pattern)
	vec3 blockPos = floor(vWorldPos + 0.001);
	vec3 localPos = fract(vWorldPos + 0.001); // 0-1 within block

	// Per-block color variation (subtle, consistent per block)
	float blockHash = hash(blockPos);
	float colorVariation = (blockHash - 0.5) * 0.08;

	// Procedural texture: noise-based pattern
	float texNoise = noise3D(vWorldPos * 4.0) * 0.06
	               + noise3D(vWorldPos * 8.0) * 0.03;

	// Edge darkening (block grid lines - subtle)
	vec3 edgeDist = min(localPos, 1.0 - localPos); // distance to nearest edge
	// Only darken edges perpendicular to the face normal
	float edgeFactor = 1.0;
	if (abs(vNormal.y) > 0.5) {
		// Top/bottom face: edges on X and Z
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.z));
	} else if (abs(vNormal.x) > 0.5) {
		// Side face X: edges on Y and Z
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.y, edgeDist.z));
	} else {
		// Side face Z: edges on X and Y
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.y));
	}
	edgeFactor = mix(0.85, 1.0, edgeFactor);

	// Apply color variation + texture noise + edges
	vec3 baseColor = vColor + colorVariation + texNoise;
	baseColor *= edgeFactor;

	// Directional sun light -- modulated by time of day
	float sunDot = max(dot(vNormal, uSunDir), 0.0);
	float ambient = 0.15 + 0.30 * uSunStrength;
	float diffuse = ambient + (1.0 - ambient) * sunDot * uSunStrength;

	// Combine: base color * lighting * face shade * AO
	vec3 lit = baseColor * diffuse * vShade * vAO;

	// Warm sun tint (stronger at dawn/dusk)
	lit += baseColor * sunDot * 0.08 * uSunStrength * vec3(1.0, 0.85, 0.6);

	// Night: slight blue ambient tint
	lit += baseColor * (1.0 - uSunStrength) * 0.03 * vec3(0.3, 0.4, 0.8);

	// Distance fog
	float dist = length(vWorldPos - uCamPos);
	float fog = smoothstep(uFogStart, uFogEnd, dist);
	lit = mix(lit, uFogColor, fog);

	float alpha = vAlpha;

	// ── Glass glare: fresnel edge brightening ──
	if (vAlpha < 0.5) {
		vec3 viewDir = normalize(uCamPos - vWorldPos);
		float cosTheta = abs(dot(vNormal, viewDir));
		float fresnel = pow(1.0 - cosTheta, 2.5);
		// Bright white glare at grazing angles
		lit = mix(lit, vec3(0.9, 0.97, 1.0), fresnel * 0.6);
		alpha = mix(vAlpha, 0.85, fresnel * 0.7);
	}

	// ── Portal glow: animated purple swirl ──
	if (vAlpha >= 0.5 && vAlpha < 1.0) {
		// Swirling pattern: sin waves on world-space coords + time
		float wave1 = sin(vWorldPos.y * 3.0 + uTime * 2.0) * 0.5 + 0.5;
		float wave2 = sin(vWorldPos.x * 2.5 - uTime * 1.3 + vWorldPos.y * 1.5) * 0.5 + 0.5;
		float swirl = mix(wave1, wave2, 0.5);
		// Deep purple to bright magenta
		vec3 portalColor = mix(vec3(0.3, 0.0, 0.8), vec3(0.9, 0.1, 1.0), swirl);
		// Add a sparkle highlight
		float sparkle = pow(swirl, 6.0) * (0.5 + 0.5 * sin(uTime * 5.0 + vWorldPos.y * 7.0));
		portalColor += vec3(0.5, 0.2, 1.0) * sparkle;
		lit = mix(lit, portalColor, 0.85);
		alpha = 0.75 + sparkle * 0.15;
	}

	fragColor = vec4(lit, alpha);
}

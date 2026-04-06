#version 410 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
in float vShade;
in float vAlpha;
in float vGlow;

uniform vec3 uSunDir;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uSunStrength; // 0..1, 0=night, 1=day
uniform float uTime;        // elapsed seconds (for animations)

out vec4 fragColor;

float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

float noise3D(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(mix(hash(i),              hash(i + vec3(1,0,0)), f.x),
		    mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
		mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
		    mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
		f.z
	);
}

void main() {
	vec3 blockPos = floor(vWorldPos + 0.001);
	vec3 localPos = fract(vWorldPos + 0.001);

	// ── Per-block variation: stronger for Dungeons-style distinct blocks ──
	float blockHash = hash(blockPos);
	float colorVariation = (blockHash - 0.5) * 0.14; // was 0.08

	// ── Material grain texture ──
	// Top/bottom faces: horizontal swirling grain
	// Side faces: vertical streaks + horizontal fissures
	float grain;
	if (abs(vNormal.y) > 0.5) {
		grain = noise3D(vWorldPos * 3.5)  * 0.06
		      + noise3D(vWorldPos * 9.0)  * 0.03
		      + noise3D(vWorldPos * 22.0) * 0.015;
	} else {
		// Vertical grain for side faces (wood/stone columns)
		grain = noise3D(vWorldPos * vec3(3.0, 10.0, 3.0)) * 0.06
		      + noise3D(vWorldPos * vec3(7.0, 22.0, 7.0)) * 0.025;
	}

	// Edge darkening (block grid lines)
	vec3 edgeDist = min(localPos, 1.0 - localPos);
	float edgeFactor;
	if (abs(vNormal.y) > 0.5) {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.z));
	} else if (abs(vNormal.x) > 0.5) {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.y, edgeDist.z));
	} else {
		edgeFactor = smoothstep(0.0, 0.06, min(edgeDist.x, edgeDist.y));
	}
	edgeFactor = mix(0.82, 1.0, edgeFactor); // slightly stronger edge contrast

	vec3 baseColor = vColor + colorVariation + grain;
	baseColor = clamp(baseColor, 0.0, 1.0);
	baseColor *= edgeFactor;

	// ── Saturation boost: Minecraft Dungeons vibrant palette ──
	float lum = dot(baseColor, vec3(0.299, 0.587, 0.114));
	baseColor = mix(vec3(lum), baseColor, 1.45); // +45% saturation push
	baseColor = clamp(baseColor, 0.0, 1.0);

	// ── Directional lighting ──
	float sunDot = max(dot(vNormal, uSunDir), 0.0);
	float ambient = 0.15 + 0.30 * uSunStrength;
	float diffuse = ambient + (1.0 - ambient) * sunDot * uSunStrength;

	vec3 lit = baseColor * diffuse * vShade * vAO;

	// Warm sun tint — peaks at dawn/dusk
	float dawnDusk = uSunStrength * (1.0 - uSunStrength) * 4.0;
	lit += baseColor * sunDot * uSunStrength * (0.10 * vec3(1.0, 0.85, 0.60)
	     + dawnDusk * 0.14 * vec3(1.0, 0.50, 0.18));

	// Night: slight blue ambient tint
	lit += baseColor * (1.0 - uSunStrength) * 0.03 * vec3(0.3, 0.4, 0.8);

	// ── Arcane surface: animated energy for magical blocks (vGlow = 1.0) ──
	if (vGlow > 0.5) {
		float t = uTime * 0.5;
		float n1 = noise3D(vWorldPos * 2.2 + vec3(t * 0.28, 0.0,    t * 0.18));
		float n2 = noise3D(vWorldPos * 5.5 - vec3(0.0,      t * 0.4, t * 0.12));
		float veins = 0.3 + 0.7 * pow(clamp(n1 * 0.6 + n2 * 0.4, 0.0, 1.0), 0.5);
		float phase = 0.5 + 0.5 * sin(t * 0.32 + vWorldPos.x * 0.14 + vWorldPos.z * 0.14);
		vec3 c1 = vec3(0.30, 0.04, 0.55);
		vec3 c2 = vec3(0.04, 0.44, 0.62);
		vec3 surfaceColor = mix(c1, c2, phase) * veins;
		lit = surfaceColor * diffuse * vShade * vAO;
	}

	// ── Distance fog ──
	float dist = length(vWorldPos - uCamPos);
	float fog = smoothstep(uFogStart, uFogEnd, dist);
	lit = mix(lit, uFogColor, fog);

	float alpha = vAlpha;

	// ── Glass glare: fresnel edge brightening ──
	if (vAlpha < 0.5) {
		vec3 viewDir = normalize(uCamPos - vWorldPos);
		float cosTheta = abs(dot(vNormal, viewDir));
		float fresnel = pow(1.0 - cosTheta, 2.5);
		lit = mix(lit, vec3(0.9, 0.97, 1.0), fresnel * 0.6);
		alpha = mix(vAlpha, 0.85, fresnel * 0.7);
	}

	fragColor = vec4(lit, alpha);
}

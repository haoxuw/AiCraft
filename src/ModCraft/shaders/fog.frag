#version 410 core

in vec3 vWorldPos;

uniform vec3 uFogColor;
uniform vec3 uCamPos;
uniform float uAlpha;
uniform float uTime;

out vec4 fragColor;

// Simple hash for animated noise
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
	// Multi-octave swirling animated fog — 4 octaves for richer texture
	float t = uTime * 0.45;
	vec3 coord = vWorldPos * 0.055;
	float n = noise3D(coord         + vec3(t * 0.50, t * 0.18, t * 0.35)) * 0.45
	        + noise3D(coord * 2.1   + vec3(t * 0.30, 0.0,      t * 0.20)) * 0.28
	        + noise3D(coord * 4.6   + vec3(0.0,      t * 0.40, t * 0.12)) * 0.17
	        + noise3D(coord * 9.3   + vec3(t * 0.15, t * 0.25, 0.0))      * 0.10;

	// Slightly cool blue-grey mist color (like real fog at distance)
	vec3 mistShift = vec3(-0.02, -0.01, 0.04);
	vec3 color = uFogColor + mistShift + (n - 0.5) * 0.06;
	color = clamp(color, 0.0, 1.0);

	// Fade alpha near chunk edges to soften boundaries
	vec3 localPos = fract(vWorldPos / 16.0);
	vec3 edgeDist = min(localPos, 1.0 - localPos) * 2.0; // 0 at edge, 1 at center
	float edgeFade = min(min(edgeDist.x, edgeDist.z), 1.0);
	edgeFade = smoothstep(0.0, 0.3, edgeFade);

	// Height gradient: fog is denser near the ground than high up
	float heightAboveCam = vWorldPos.y - uCamPos.y;
	float heightFade = 1.0 - clamp(heightAboveCam / 40.0, 0.0, 0.65);

	float alpha = uAlpha * (0.72 + n * 0.28) * edgeFade * heightFade;
	fragColor = vec4(color, alpha);
}

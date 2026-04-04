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
		mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x),
		    mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
		mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
		    mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
		f.z
	);
}

void main() {
	// Slow-swirling animated fog
	float t = uTime * 0.3;
	vec3 noiseCoord = vWorldPos * 0.05 + vec3(t * 0.4, t * 0.15, t * 0.3);
	float n = noise3D(noiseCoord) * 0.5 + noise3D(noiseCoord * 2.3) * 0.3;

	// Fog color: blend between sky/horizon color with slight variation
	vec3 color = uFogColor + (n - 0.4) * 0.08;

	// Fade alpha near chunk edges (soften boundaries)
	vec3 localPos = fract(vWorldPos / 16.0);
	vec3 edgeDist = min(localPos, 1.0 - localPos) * 2.0; // 0 at edge, 1 at center
	float edgeFade = min(min(edgeDist.x, edgeDist.z), 1.0);
	edgeFade = smoothstep(0.0, 0.3, edgeFade);

	float alpha = uAlpha * (0.7 + n * 0.3) * edgeFade;
	fragColor = vec4(color, alpha);
}

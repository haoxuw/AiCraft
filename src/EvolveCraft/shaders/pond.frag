#version 410 core

in  vec3 vWorldPos;
out vec4 FragColor;

uniform float uRadius;
uniform float uTime;
uniform vec3  uCamera;

// Pond shader — animated water with:
//  * radial gradient from deep blue at center to teal at the edge
//  * two-scale sine caustic shimmer
//  * soft circular fade at the boundary
void main() {
	float r2 = vWorldPos.x*vWorldPos.x + vWorldPos.z*vWorldPos.z;
	float r  = sqrt(r2);
	float t  = clamp(r / uRadius, 0.0, 1.0);

	// depth / radial gradient
	vec3 deep    = vec3(0.025, 0.08, 0.18);
	vec3 mid     = vec3(0.04,  0.16, 0.30);
	vec3 shallow = vec3(0.10,  0.32, 0.42);
	vec3 base = mix(mid, deep, smoothstep(0.1, 0.8, 1.0 - t));
	base = mix(base, shallow, smoothstep(0.7, 1.0, t) * 0.6);

	// caustic shimmer (two scales, different speeds + angles)
	vec2 p = vWorldPos.xz;
	float c1 = sin(p.x * 0.35 + uTime * 0.6) * sin(p.y * 0.35 + uTime * 0.43);
	float c2 = sin(p.x * 0.11 - uTime * 0.25) * sin(p.y * 0.13 + uTime * 0.31);
	float caustics = c1 * 0.5 + c2 * 0.25;
	base += vec3(0.06, 0.10, 0.15) * caustics;

	// soft boundary fade
	float edge = smoothstep(uRadius * 0.98, uRadius, r);
	base = mix(base, vec3(0.0, 0.04, 0.06), edge);

	// subtle vignette toward camera for depth
	base *= 1.0 - 0.15 * smoothstep(0.0, uRadius, r);

	FragColor = vec4(base, 1.0);
}

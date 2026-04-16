#version 450

layout(location = 0) in vec3 vRayDir;

layout(push_constant) uniform PC {
	mat4 invVP;
	vec4 sunDir; // xyz, w = sunStrength
} pc;

layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
	p = fract(p * vec3(443.8975, 397.2973, 491.1871));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

float noise2D(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(hash(vec3(i, 0)),            hash(vec3(i + vec2(1,0), 0)), f.x),
		mix(hash(vec3(i + vec2(0,1), 0)), hash(vec3(i + vec2(1,1), 0)), f.x),
		f.y);
}

void main() {
	vec3 dir = normalize(vRayDir);
	vec3 sun = normalize(pc.sunDir.xyz);
	float sunStr = pc.sunDir.w;

	// ═══════════════════════════════════════════
	// Dungeons-style warm sky — golden, not cold blue
	// ═══════════════════════════════════════════

	// Zenith: deep warm blue (not cold cyan)
	vec3 zenith = mix(vec3(0.06, 0.05, 0.15), vec3(0.20, 0.35, 0.72), sunStr);
	// Horizon: warm golden peach
	vec3 horizon = mix(vec3(0.12, 0.10, 0.18), vec3(0.78, 0.60, 0.42), sunStr);

	float t = pow(max(dir.y, 0.0), 0.35);
	vec3 sky = mix(horizon, zenith, t);

	// Below horizon
	if (dir.y < 0.0) {
		vec3 ground = horizon * vec3(0.6, 0.55, 0.45);
		sky = mix(horizon, ground, min(-dir.y * 5.0, 1.0));
	}

	// ── Sun (large, warm, golden — Dungeons has a big warm sun glow) ──
	float sunDot = max(dot(dir, sun), 0.0);
	sky += horizon * pow(sunDot, 3.0) * 0.20 * sunStr;
	sky += vec3(1.0, 0.82, 0.50) * pow(sunDot, 6.0) * 0.40 * sunStr;
	sky += vec3(1.0, 0.90, 0.65) * pow(sunDot, 32.0) * 0.8;
	sky += vec3(1.0, 0.98, 0.85) * pow(sunDot, 512.0) * 3.0;

	// Sunset horizon glow (broad golden wash)
	float horizGlow = pow(max(1.0 - abs(dir.y), 0.0), 3.0);
	sky += vec3(1.0, 0.55, 0.20) * horizGlow * 0.25 * sunStr;

	// ── Clouds (warm-tinted) ──
	float cloudVis = smoothstep(0.02, 0.15, dir.y);
	if (cloudVis > 0.001) {
		vec2 uv = dir.xz / max(dir.y, 0.03) * 0.35;
		float cn = noise2D(uv * 1.0) * 0.50
		         + noise2D(uv * 2.3 + vec2(0.5, 0.7)) * 0.30
		         + noise2D(uv * 5.1 + vec2(1.2, 0.3)) * 0.20;
		cn = smoothstep(0.48, 0.68, cn);

		vec3 cloudLit = mix(vec3(0.90, 0.65, 0.40), vec3(0.98, 0.92, 0.82), sunStr);
		vec3 cloudShade = cloudLit * 0.6;
		vec3 cloudColor = mix(cloudShade, cloudLit, 0.7);
		sky = mix(sky, cloudColor, cn * cloudVis);
	}

	outColor = vec4(sky, 1.0);
}

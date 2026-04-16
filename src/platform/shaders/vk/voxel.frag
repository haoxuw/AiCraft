#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in float vDist;

layout(push_constant) uniform PC {
	mat4 viewProj;
	vec4 camPos;   // xyz, w = time
	vec4 sunDir;   // xyz, w = sunStrength
} pc;

// Shadow map — sampled per-fragment to cut off direct sun contribution where
// occluded. UBO carries the sun's view-projection so we can reproject worldPos.
layout(set = 0, binding = 0) uniform sampler2D uShadowMap;
layout(set = 0, binding = 1) uniform ShadowUBO {
	mat4 shadowVP;
	vec4 shadowParams;   // x = texel size (1/res), y = bias, zw = pad
} shadow;

layout(location = 0) out vec4 outColor;

// ── Noise ──
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

// Slope-scaled PCF shadow sampling. Returns 1.0 (fully lit) down to 0.0
// (fully in shadow) based on 3×3 taps around the projected shadow texel.
float sampleShadow(vec3 worldPos, vec3 n, vec3 sun) {
	vec4 sp = shadow.shadowVP * vec4(worldPos, 1.0);
	vec3 ndc = sp.xyz / sp.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
	// Outside the shadow frustum = unshadowed (big world, small map).
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
	// Vulkan depth is [0, 1]; clip if behind near or past far.
	if (ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

	// Slope-scaled bias — steeper normals relative to the sun need more bias
	// to avoid acne on grazing geometry.
	float cosT = max(dot(n, sun), 0.0);
	float slope = sqrt(1.0 - cosT * cosT) / max(cosT, 0.05);
	float bias = shadow.shadowParams.y * (1.0 + slope * 2.0);
	float compareZ = ndc.z - bias;

	float texel = shadow.shadowParams.x;
	float acc = 0.0;
	for (int x = -1; x <= 1; x++) {
		for (int y = -1; y <= 1; y++) {
			vec2 off = vec2(float(x), float(y)) * texel;
			float d = texture(uShadowMap, uv + off).r;
			acc += compareZ < d ? 1.0 : 0.0;
		}
	}
	return acc / 9.0;
}

void main() {
	vec3 n = normalize(vNormal);
	vec3 sun = normalize(pc.sunDir.xyz);
	float sunStr = pc.sunDir.w;

	vec3 blockPos = floor(vWorldPos + 0.001);
	vec3 localPos = fract(vWorldPos + 0.001);

	// ── Per-block color variation (strong, Dungeons-style distinct blocks) ──
	float blockHash = hash(blockPos);
	float colorVar = (blockHash - 0.5) * 0.16;

	// ── Material grain (heavier texture for stone/wood feel) ──
	float grain;
	if (abs(n.y) > 0.5) {
		grain = noise3D(vWorldPos * 3.5) * 0.07
		      + noise3D(vWorldPos * 9.0) * 0.04
		      + noise3D(vWorldPos * 22.0) * 0.02;
	} else {
		grain = noise3D(vWorldPos * vec3(3.0, 10.0, 3.0)) * 0.07
		      + noise3D(vWorldPos * vec3(7.0, 22.0, 7.0)) * 0.03;
	}

	// ── Edge darkening (crisp block grid — Dungeons has very visible edges) ──
	vec3 edgeDist = min(localPos, 1.0 - localPos);
	float edgeFactor;
	if (abs(n.y) > 0.5)      edgeFactor = smoothstep(0.0, 0.08, min(edgeDist.x, edgeDist.z));
	else if (abs(n.x) > 0.5) edgeFactor = smoothstep(0.0, 0.08, min(edgeDist.y, edgeDist.z));
	else                      edgeFactor = smoothstep(0.0, 0.08, min(edgeDist.x, edgeDist.y));
	edgeFactor = mix(0.72, 1.0, edgeFactor);

	vec3 baseColor = clamp(vColor + colorVar + grain, 0.0, 1.0);
	baseColor *= edgeFactor;

	// Mild saturation (final boost in composite pass)
	float lum = dot(baseColor, vec3(0.299, 0.587, 0.114));
	baseColor = clamp(mix(vec3(lum), baseColor, 1.25), 0.0, 1.0);

	// ═══════════════════════════════════════════════════════════
	// DUNGEONS-STYLE LIGHTING — shadow-map-driven, warm, high contrast
	// ═══════════════════════════════════════════════════════════

	// Shadow map drives the big lighting decision: is this fragment in the
	// sun's path or not? Per-face normal dot product shapes intensity. No
	// hardcoded face brightness — shadow map + dot product do that naturally.
	float sunDot = max(dot(n, sun), 0.0);
	float shadowK = sampleShadow(vWorldPos, n, sun);
	float direct = sunDot * sunStr * shadowK;

	// Warm low ambient. Shadowed areas land here and read clearly as shadow.
	vec3 warmAmbient = vec3(0.22, 0.18, 0.14) * sunStr + vec3(0.07, 0.06, 0.09);

	// Rich golden sun, strong enough to make lit faces pop against shadow.
	vec3 sunColor = vec3(1.15, 0.95, 0.62);

	vec3 lit = baseColor * (warmAmbient + sunColor * direct);

	// Sky fill — blueish bounce for top-ish faces, independent of shadow
	// (sky is huge, always visible where the normal points upward).
	float skyFill = max(n.y, 0.0) * 0.18;
	lit += baseColor * vec3(0.42, 0.52, 0.70) * skyFill * sunStr;

	// Warm ground bounce from below (like Dungeons' warm reflected fill).
	float groundBounce = max(-n.y, 0.0) * 0.10;
	lit += baseColor * vec3(0.80, 0.55, 0.32) * groundBounce * sunStr;

	// Bottom faces (overhangs): extra darken since nothing shines on them.
	if (n.y < -0.5) lit *= 0.55;

	// Warm rim on side faces for block-edge definition.
	if (abs(n.y) < 0.5) {
		vec3 viewDir = normalize(pc.camPos.xyz - vWorldPos);
		float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
		lit += vec3(0.20, 0.14, 0.06) * rim * sunStr * 0.4;
	}

	// ── Distance fog → warm horizon (Dungeons fog is golden, not grey) ──
	vec3 fogColor = mix(
		vec3(0.12, 0.10, 0.18),                    // night
		vec3(0.72, 0.62, 0.48) * 0.9 + vec3(0.15, 0.20, 0.30) * 0.1, // day: golden haze
		sunStr
	);
	float fogStart = 40.0;
	float fogEnd = 80.0;
	float fog = smoothstep(fogStart, fogEnd, vDist);
	lit = mix(lit, fogColor, fog);

	outColor = vec4(lit, 1.0);
}

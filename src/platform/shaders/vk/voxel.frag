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

	// Neutral lighting: shadow map + sun dot for direction, grey ambient so
	// hue comes from the material only. Any global grading/saturation/warmth
	// is owned by the composite UBO (set via the Render Tuning panel).
	float sunDot = max(dot(n, sun), 0.0);
	float shadowK = sampleShadow(vWorldPos, n, sun);
	float direct = sunDot * sunStr * shadowK;

	vec3 ambient = vec3(0.32) * sunStr + vec3(0.08);
	vec3 lit = baseColor * (ambient + vec3(direct));

	// Sky/ground fills kept neutral (gentle hemisphere), no hue tinting.
	float skyFill = max(n.y, 0.0) * 0.10;
	lit += baseColor * skyFill * sunStr;
	float groundBounce = max(-n.y, 0.0) * 0.05;
	lit += baseColor * groundBounce * sunStr;

	// Overhangs still read darker so geometry is legible.
	if (n.y < -0.5) lit *= 0.65;

	// Neutral distance fog (night→day stays grey; colored sky bleed is the
	// composite pass's job, not this shader's).
	vec3 fogColor = mix(vec3(0.12), vec3(0.62), sunStr);
	float fogStart = 40.0;
	float fogEnd = 80.0;
	float fog = smoothstep(fogStart, fogEnd, vDist);
	lit = mix(lit, fogColor, fog);

	outColor = vec4(lit, 1.0);
}

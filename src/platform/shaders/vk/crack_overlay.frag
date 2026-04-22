#version 450

// Voronoi cell-edge crack overlay.
//
// Per fragment:
//   1. Pick the block face from argmax of |localPos - 0.5|. Get face-UV in [0,1].
//   2. Voronoi: scale UV by cellsPerFace, jitter seeds via hash, find F1/F2
//      (distances to nearest two cell seeds). Cell edges ≈ where F2-F1 is small.
//   3. Reveal mask: each cell decides if it "glows" based on hash(cellId) < stageFrac.
//      Stage 0 reveals ~35% of edges, stage 1 ~65%, stage 2 ~95%. Lower-stage
//      edges remain in higher stages — cracks GROW, not shuffle.
//   4. Glow: amber → white-hot with stage. Global time pulse.

layout(push_constant) uniform PC {
	mat4  viewProj;
	vec4  aabbMin;    // xyz = min corner, w = stage (0,1,2)
	vec4  aabbMax;    // xyz = max corner, w = time (seconds)
} pc;

layout(location = 0) in vec3 vLocalPos;
layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
	p = fract(p * vec2(234.34, 435.345));
	p += dot(p, p + 34.23);
	return fract(p.x * p.y);
}

vec2 hash22(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

// Returns (F1, F2, reveal) — distances to nearest and 2nd-nearest seed,
// plus whether the nearest cell is lit based on stageFrac.
vec3 voronoiCrack(vec2 uv, float cells, float stageFrac, float seedOffset) {
	vec2 p  = uv * cells;
	vec2 pi = floor(p);
	vec2 pf = fract(p);

	float f1 = 8.0, f2 = 8.0;
	vec2  f1Cell = vec2(0.0);

	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			vec2 n    = vec2(float(dx), float(dy));
			vec2 cell = pi + n;
			vec2 seed = hash22(cell + seedOffset);
			vec2 rp   = n + seed - pf;
			float d = length(rp);
			if (d < f1) { f2 = f1; f1 = d; f1Cell = cell; }
			else if (d < f2) { f2 = d; }
		}
	}

	// Stable reveal: adding cell index to the hash offset keeps the
	// per-cell decision stable as stageFrac grows.
	float reveal = step(hash21(f1Cell + seedOffset * 0.37), stageFrac);
	return vec3(f1, f2, reveal);
}

void main() {
	vec3 d = abs(vLocalPos - 0.5);

	// Pick face from argmax of |localPos-0.5|. axisKey seeds the Voronoi
	// hash per face so the 6 faces show different patterns.
	vec2 uv;
	float axisKey;
	if (d.x >= d.y && d.x >= d.z) {
		uv = vec2(vLocalPos.z, vLocalPos.y);
		axisKey = vLocalPos.x > 0.5 ? 1.0 : 2.0;
	} else if (d.y >= d.z) {
		uv = vec2(vLocalPos.x, vLocalPos.z);
		axisKey = vLocalPos.y > 0.5 ? 3.0 : 4.0;
	} else {
		uv = vec2(vLocalPos.x, vLocalPos.y);
		axisKey = vLocalPos.z > 0.5 ? 5.0 : 6.0;
	}

	float stage = pc.aabbMin.w;
	float stageFrac = stage < 0.5 ? 0.35
	                : stage < 1.5 ? 0.65
	                :               0.95;

	// Fixed Voronoi density — changing cells between stages would
	// repartition the face and reshuffle the pattern.
	const float cells = 9.0;
	float blockSeed = dot(floor(pc.aabbMin.xyz), vec3(73.856, 19.349, 83.493));
	vec3 v = voronoiCrack(uv, cells, stageFrac, axisKey * 17.3 + blockSeed);

	float edgeDist = v.y - v.x;
	const float lineW = 0.05;
	float mask = 1.0 - smoothstep(lineW, lineW + 0.02, edgeDist);
	mask *= v.z;

	// Amber → bright amber → white-hot as damage deepens.
	vec3 cAmber = vec3(1.00, 0.35, 0.08);
	vec3 cHot   = vec3(1.00, 0.85, 0.55);
	vec3 glow   = mix(cAmber, cHot, stage * 0.5);

	float pulseRate = 4.0 + 4.0 * stage;
	float pulse     = 0.65 + 0.35 * sin(pc.aabbMax.w * pulseRate);
	float intensity = (0.7 + 0.4 * stage) * pulse;

	// Faint baseline glow so the block reads as "charged" between lines.
	float ambient = 0.04 * (stage + 1.0);
	float a = (mask * intensity) + ambient * pulse;

	outColor = vec4(glow * a, a);
}

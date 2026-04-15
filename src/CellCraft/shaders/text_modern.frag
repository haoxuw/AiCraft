#version 410 core

// SdfFont fragment shader — alpha-atlas glyph with optional neon halo.
//
// The atlas stores a high-res grayscale bitmap (no true SDF). When sampled
// at a smaller output size with GL_LINEAR, the edges have a soft alpha
// gradient that we can treat as a pseudo-distance for shading.
//
// Halo: instead of an axis-aligned gaussian (which produced visible
// plus-shaped / banded constructive-interference artifacts on display-size
// glyphs like "YOU DIED"), we sample at 12 spatially-distributed points on
// two concentric rotated rings. This gives a smoother, isotropic falloff
// with no horizontal/vertical stripe pattern.
//
// The per-glyph uv box (v_uv_box = u0,v0,u1,v1) is used to reject halo
// samples that land in neighbour glyph cells (which would otherwise bleed
// foreign glyph shapes into the halo). The geometry is expanded outward
// by the bake-time padding, so v_uv can fall outside the tight box —
// samples there correctly read zero.

in vec2 v_uv;
in vec4 v_uv_box; // (u0, v0, u1, v1) — the glyph's tight atlas cell
out vec4 FragColor;

uniform sampler2D u_atlas;
uniform vec4  u_fill;
uniform vec4  u_glow;
uniform float u_glow_radius;      // in uv units
uniform float u_glow_intensity;   // scalar multiplier on halo alpha
uniform float u_atlas_texel;      // 1/atlas_w (square atlas assumed)

float sampleA(vec2 uv) {
	// Reject any sample outside the glyph's tight cell — the area around each
	// glyph in the packed atlas belongs to a neighbour. Returning 0 produces
	// the natural halo falloff at the glyph edge.
	if (uv.x < v_uv_box.x || uv.x > v_uv_box.z ||
	    uv.y < v_uv_box.y || uv.y > v_uv_box.w) return 0.0;
	return texture(u_atlas, uv).r;
}

// Halo alpha: 12-tap rotated ring sampler. Two rings (inner @ 0.55*r,
// outer @ r) each with 6 taps at a 30° offset between rings, plus a
// centre tap. This is NOT axis-aligned, so there is no plus-pattern
// constructive interference.
float haloAlpha(vec2 uv, float r) {
	float a = sampleA(uv) * 0.30;
	// Outer ring, 6 taps at 0,60,120,180,240,300 deg.
	const float TAU = 6.2831853;
	float w_out = 0.07;
	for (int i = 0; i < 6; ++i) {
		float ang = TAU * (float(i) / 6.0);
		vec2 off = vec2(cos(ang), sin(ang)) * r;
		a += sampleA(uv + off) * w_out;
	}
	// Inner ring, 6 taps at 30,90,150,210,270,330 deg, half radius.
	float w_in = 0.10;
	float r_in = r * 0.55;
	for (int i = 0; i < 6; ++i) {
		float ang = TAU * (float(i) / 6.0 + 1.0 / 12.0);
		vec2 off = vec2(cos(ang), sin(ang)) * r_in;
		a += sampleA(uv + off) * w_in;
	}
	return a;
}

void main() {
	// Core glyph alpha (strict, no neighbour bleed).
	float core = sampleA(v_uv);

	// Mild anti-alias boost: remap core through a smoothstep for crisper
	// edges even when atlas is softly sampled.
	float aa = smoothstep(0.15, 0.65, core);

	vec4 outCol = u_fill * vec4(1.0, 1.0, 1.0, aa);

	if (u_glow.a > 0.0 && u_glow_radius > 0.0) {
		float ga = haloAlpha(v_uv, u_glow_radius);
		ga = clamp(ga * 1.4, 0.0, 1.0);
		ga = pow(ga, 0.85) * u_glow_intensity;
		// Composite: halo below, fill on top. Straight-alpha compositing
		// in premultiplied space to avoid stripe-ish additive interference.
		vec3 glowRGB = u_glow.rgb * (ga * u_glow.a);
		float glowA  = ga * u_glow.a;
		vec3 fillRGB = outCol.rgb * outCol.a;
		float fillA  = outCol.a;
		// Premultiplied OVER: C = Cf + Cb * (1 - Af).
		vec3 rgb = fillRGB + glowRGB * (1.0 - fillA);
		float a  = fillA  + glowA   * (1.0 - fillA);
		if (a > 0.001) {
			outCol = vec4(rgb / a, a);
		} else {
			outCol = vec4(0.0);
		}
	}

	if (outCol.a < 0.002) discard;
	FragColor = outCol;
}

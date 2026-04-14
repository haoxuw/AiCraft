#version 410 core

// Chalk fragment shader.
//
// Visual model: a core line of near-full opacity with feathered edges that
// fall off to zero alpha. NumptyPhysics got the soft edge by doing it in
// geometry (a 10-vertex fan per segment). We do it in the shader instead:
// the ribbon is just two rows (thin), and alpha = window(across) * grit(along).
//
// Grit: chalk is never uniform — it's a broken pattern of dense pigment
// separated by gaps where the stick missed the board's tooth. Two octaves
// of hash noise along the stroke length reproduce that without needing a
// texture atlas.

in float v_across;   // -1 .. +1 across ribbon width
in float v_along;    // pixels from stroke start
out vec4 f_color;

uniform vec3  u_color;
uniform float u_half_width;  // pixels, same value used by the CPU ribbon

float hash11(float x) {
	return fract(sin(x * 12.9898) * 43758.5453);
}

float hash21(vec2 p) {
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
	// Feathered alpha across the width. |across|=0 → full, =1 → zero.
	// The transition is wide enough that the edge reads as chalk dust
	// rather than a hard line with a tiny outline.
	float a_edge = 1.0 - smoothstep(0.45, 1.0, abs(v_across));
	if (a_edge <= 0.0) discard;

	// Grit: sample two frequencies of along-length noise, modulated by a
	// slight across-width variation so the grit isn't in perfect vertical
	// bands.
	float n1 = hash11(floor(v_along * 0.8));
	float n2 = hash21(vec2(floor(v_along * 0.22), floor(v_across * 3.0 + 2.0)));
	float grit = mix(n1, n2, 0.45);

	// Map grit into an alpha attenuation. Most of the stroke stays bright;
	// 15-30% of it has small gaps where "pigment missed the board".
	float a_grit = mix(0.75, 1.0, smoothstep(0.18, 0.65, grit));

	// Slight color jitter — pigment isn't uniform in hue either. Against a
	// bright cream paper we want the stroke to read as saturated marker,
	// so push toward deeper color rather than lifting toward white.
	vec3 col = u_color + (grit - 0.5) * 0.06;
	col = mix(col * 0.85, col, a_grit);

	f_color = vec4(col, a_edge * a_grit);
}

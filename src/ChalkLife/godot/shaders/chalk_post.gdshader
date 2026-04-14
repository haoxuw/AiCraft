// ChalkLife — chalk post-process overlay
//
// Applied to a fullscreen ColorRect on the top CanvasLayer. Samples
// the rendered frame below (SCREEN_TEXTURE) and re-composits each
// non-background pixel with a chalk-dust grain + breaks, turning
// clean strokes into plausible chalk.
//
// Technique, in three layers:
//   1) Edge-aware grain — high-frequency value noise multiplied
//      over the stroke alpha. Produces the speckled dust look.
//   2) Skip-stroke breaks — a lower-frequency noise threshold
//      punches occasional holes in the stroke, like a tired piece
//      of chalk that skips.
//   3) Faint chalk-board grain on the background — a very low-
//      contrast noise that keeps the board from looking CG-flat.

shader_type canvas_item;

uniform sampler2D SCREEN_TEXTURE : hint_screen_texture, filter_linear;
uniform float grain_strength : hint_range(0.0, 1.0) = 0.55;
uniform float break_threshold : hint_range(0.0, 1.0) = 0.12;
uniform float board_grain : hint_range(0.0, 0.3) = 0.04;
uniform vec3  board_color = vec3(0.055, 0.085, 0.06);

// Cheap hash/noise — good enough for grain; we're not rendering
// cloud physics. Ken Perlin would politely decline to review this.
float hash(vec2 p) {
	p = fract(p * vec2(443.897, 441.423));
	p += dot(p, p.yx + 19.19);
	return fract((p.x + p.y) * p.x);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float a = hash(i);
	float b = hash(i + vec2(1.0, 0.0));
	float c = hash(i + vec2(0.0, 1.0));
	float d = hash(i + vec2(1.0, 1.0));
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void fragment() {
	vec2 uv = SCREEN_UV;
	vec4 src = texture(SCREEN_TEXTURE, uv);

	// Treat the board color as "empty"; anything brighter is a stroke.
	float stroke_mask = clamp(
		(src.r + src.g + src.b - (board_color.r + board_color.g + board_color.b)) * 1.3,
		0.0, 1.0);

	// Grain: multiply chalk pixels by 1 - strength*noise.
	float g = vnoise(uv * vec2(1600.0, 900.0));
	float grain = 1.0 - grain_strength * g;

	// Skip breaks: occasional larger gaps along strokes.
	float br = vnoise(uv * vec2(220.0, 220.0));
	float skip = step(break_threshold, br);

	vec3 chalk_rgb = src.rgb * grain * skip;

	// Low-frequency board grain — keeps empty space alive.
	float bg = vnoise(uv * vec2(380.0, 420.0));
	vec3 bg_rgb = board_color + vec3(bg * board_grain - board_grain * 0.5);

	// Compose: bg where no stroke, chalk where stroke.
	vec3 outc = mix(bg_rgb, chalk_rgb, stroke_mask);
	COLOR = vec4(outc, 1.0);
}

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform PC {
	mat4 invVP;
	mat4 vp;
} pc;

float hash(vec2 p) {
	p = fract(p * vec2(443.897, 397.297));
	p += dot(p, p + 19.19);
	return fract(p.x * p.y);
}

vec3 worldPos(vec2 uv, float d) {
	vec4 c = vec4(uv * 2.0 - 1.0, d, 1.0);
	vec4 w = pc.invVP * c;
	return w.xyz / w.w;
}

// ── FXAA ──

vec3 fxaa() {
	vec2 texel = 1.0 / vec2(textureSize(colorTex, 0));
	vec3 rgbM  = texture(colorTex, vUV).rgb;
	vec3 rgbNW = texture(colorTex, vUV + vec2(-1, -1) * texel).rgb;
	vec3 rgbNE = texture(colorTex, vUV + vec2( 1, -1) * texel).rgb;
	vec3 rgbSW = texture(colorTex, vUV + vec2(-1,  1) * texel).rgb;
	vec3 rgbSE = texture(colorTex, vUV + vec2( 1,  1) * texel).rgb;

	vec3 lc = vec3(0.299, 0.587, 0.114);
	float lumM  = dot(rgbM, lc);
	float lumNW = dot(rgbNW, lc);
	float lumNE = dot(rgbNE, lc);
	float lumSW = dot(rgbSW, lc);
	float lumSE = dot(rgbSE, lc);

	float lumMin = min(lumM, min(min(lumNW, lumNE), min(lumSW, lumSE)));
	float lumMax = max(lumM, max(max(lumNW, lumNE), max(lumSW, lumSE)));
	if ((lumMax - lumMin) < max(0.0312, lumMax * 0.125))
		return rgbM;

	vec2 dir;
	dir.x = -((lumNW + lumNE) - (lumSW + lumSE));
	dir.y =  ((lumNW + lumSW) - (lumNE + lumSE));
	float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y))
		+ max((lumNW + lumNE + lumSW + lumSE) * 0.03125, 0.0078125));
	dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * texel;

	vec3 rgbA = 0.5 * (
		texture(colorTex, vUV + dir * (1.0/3.0 - 0.5)).rgb +
		texture(colorTex, vUV + dir * (2.0/3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (
		texture(colorTex, vUV + dir * -0.5).rgb +
		texture(colorTex, vUV + dir *  0.5).rgb);
	float lumB = dot(rgbB, lc);
	return (lumB < lumMin || lumB > lumMax) ? rgbA : rgbB;
}

// ── SSAO (subtle, small radius) ──

const vec3 AO_KERNEL[8] = vec3[8](
	vec3( 0.053, 0.084, 0.022), vec3(-0.068, 0.039, 0.046),
	vec3( 0.088,-0.059, 0.079), vec3(-0.041, 0.094, 0.036),
	vec3( 0.125, 0.043, 0.155), vec3(-0.153, 0.074, 0.028),
	vec3( 0.048,-0.159, 0.085), vec3(-0.085, 0.025, 0.184)
);

float ssao() {
	float depth = texture(depthTex, vUV).r;
	if (depth >= 0.999) return 1.0;

	vec2 texel = 1.0 / vec2(textureSize(depthTex, 0));
	vec3 origin = worldPos(vUV, depth);

	float dR = texture(depthTex, vUV + vec2(texel.x, 0)).r;
	float dL = texture(depthTex, vUV - vec2(texel.x, 0)).r;
	float dU = texture(depthTex, vUV + vec2(0, texel.y)).r;
	float dD = texture(depthTex, vUV - vec2(0, texel.y)).r;

	vec3 ddx = abs(dR - depth) < abs(dL - depth)
		? worldPos(vUV + vec2(texel.x, 0), dR) - origin
		: origin - worldPos(vUV - vec2(texel.x, 0), dL);
	vec3 ddy = abs(dU - depth) < abs(dD - depth)
		? worldPos(vUV + vec2(0, texel.y), dU) - origin
		: origin - worldPos(vUV - vec2(0, texel.y), dD);

	vec3 normal = normalize(cross(ddx, ddy));
	vec3 nearPt = worldPos(vUV, 0.0);
	if (dot(normal, nearPt - origin) < 0.0) normal = -normal;

	float angle = hash(gl_FragCoord.xy) * 6.2832;
	float c = cos(angle), s = sin(angle);
	vec3 t = abs(normal.y) < 0.99
		? normalize(cross(normal, vec3(0, 1, 0)))
		: normalize(cross(normal, vec3(1, 0, 0)));
	vec3 b = cross(normal, t);
	vec3 rt = t * c + b * s;
	vec3 rb = cross(normal, rt);
	mat3 tbn = mat3(rt, rb, normal);

	float radius = 0.6;
	float occ = 0.0;
	for (int i = 0; i < 8; i++) {
		vec3 sw = origin + tbn * AO_KERNEL[i] * radius;
		vec4 sc = pc.vp * vec4(sw, 1.0);
		if (sc.w <= 0.0) continue;
		vec2 suv = (sc.xy / sc.w) * 0.5 + 0.5;
		float sd = sc.z / sc.w;
		if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

		float sceneD = texture(depthTex, suv).r;
		float delta = sd - sceneD;
		occ += smoothstep(0.0, 0.002, delta) * (1.0 - smoothstep(0.0, 0.03, delta));
	}
	return clamp(1.0 - occ * 0.8 / 8.0, 0.0, 1.0);
}

// ── Bloom (gentle, only bright spots) ──

vec3 bloom() {
	vec2 texel = 1.0 / vec2(textureSize(colorTex, 0));
	vec3 b = vec3(0);
	float thr = 0.6;
	float tw = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			float d = length(vec2(x, y));
			if (d > 2.5) continue;
			float w = exp(-d * d * 0.25);
			vec3 s = texture(colorTex, vUV + vec2(x, y) * texel * 2.0).rgb;
			b += max(s - thr, vec3(0)) * w;
			tw += w;
		}
	}
	b /= tw;
	b *= vec3(1.0, 0.9, 0.7);
	return b;
}

// ── ACES tone mapping ──

vec3 aces(vec3 x) {
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float luminance(vec3 c) {
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Proper piecewise sRGB OETF (linear → sRGB). Our swap surface is UNORM, so
// the display gamma is NOT applied by the hardware — we encode here.
vec3 linearToSrgb(vec3 x) {
	vec3 hi = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
	vec3 lo = 12.92 * x;
	return mix(hi, lo, step(x, vec3(0.0031308)));
}

void main() {
	vec3 color = fxaa();

	float ao = ssao();
	color *= mix(0.62, 1.0, ao);

	color += bloom() * 0.18;
	color = aces(color * 0.95);         // gentle exposure — peaceful, not punchy
	color *= vec3(1.00, 1.00, 0.98);    // very subtle warm

	// Faint S-curve: crush blacks a hair, keep highlights soft.
	color = mix(vec3(0.18), color, 1.04);

	// Saturation: just enough to feel natural, not cartoonish.
	float gray = luminance(color);
	color = mix(vec3(gray), color, 1.06);

	vec2 vc = vUV - 0.5;
	color *= 1.0 - dot(vc, vc) * 0.18;

	color = linearToSrgb(clamp(color, 0.0, 1.0));
	outColor = vec4(color, 1.0);
}

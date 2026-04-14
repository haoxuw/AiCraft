#version 410 core
in vec2 v_uv;
out vec4 frag;

uniform sampler2D u_tex;
uniform vec2 u_texel;     // 1/texture size
uniform vec2 u_dir;       // (1,0) for horizontal, (0,1) for vertical

// 9-tap Gaussian. weights for sigma ~= 4.
const float W[5] = float[5](0.2270270270, 0.1945945946, 0.1216216216,
                            0.0540540541, 0.0162162162);

void main() {
	vec3 acc = texture(u_tex, v_uv).rgb * W[0];
	for (int i = 1; i < 5; ++i) {
		vec2 off = u_dir * u_texel * float(i) * 2.0;
		acc += texture(u_tex, v_uv + off).rgb * W[i];
		acc += texture(u_tex, v_uv - off).rgb * W[i];
	}
	frag = vec4(acc, 1.0);
}

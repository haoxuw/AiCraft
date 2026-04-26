#version 450

// Sample the CEF BGRA texture and emit it pre-multiplied alpha-blended.
// Image is VK_FORMAT_B8G8R8A8_UNORM; the swapchain is also B8G8R8A8_UNORM,
// so a direct pass-through preserves byte order without swizzling.
layout(set = 0, binding = 0) uniform sampler2D cefTex;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(cefTex, vUV);
}

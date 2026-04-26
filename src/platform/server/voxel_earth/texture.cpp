#include "server/voxel_earth/texture.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO     // we feed bytes from memory, never paths
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace civcraft::voxel_earth {

void Texture::sample(float u, float v, uint8_t out[4]) const {
	if (empty()) {
		out[0] = out[1] = out[2] = 0;
		out[3] = 255;
		return;
	}
	u = std::clamp(u, 0.0f, 1.0f);
	v = std::clamp(v, 0.0f, 1.0f);

	const float fx = u * static_cast<float>(width  - 1);
	const float fy = v * static_cast<float>(height - 1);
	const int   x0 = static_cast<int>(std::floor(fx));
	const int   y0 = static_cast<int>(std::floor(fy));
	const int   x1 = std::min(x0 + 1, width  - 1);
	const int   y1 = std::min(y0 + 1, height - 1);
	const float tx = fx - static_cast<float>(x0);
	const float ty = fy - static_cast<float>(y0);

	auto px = [&](int x, int y, int c) -> float {
		return rgba[(y * width + x) * 4 + c];
	};
	for (int c = 0; c < 4; ++c) {
		float p00 = px(x0, y0, c);
		float p10 = px(x1, y0, c);
		float p01 = px(x0, y1, c);
		float p11 = px(x1, y1, c);
		float top    = p00 * (1.0f - tx) + p10 * tx;
		float bottom = p01 * (1.0f - tx) + p11 * tx;
		float val    = top * (1.0f - ty) + bottom * ty;
		out[c] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
	}
}

bool decode_image(const EmbeddedImage& src, Texture& out, std::string* error) {
	if (src.bytes.empty()) {
		if (error) *error = "image is empty";
		return false;
	}
	if (src.format == "rgba8") {
		// Already raw RGBA8: width*height*4 == bytes.size(); we can't recover
		// dims from EmbeddedImage alone, so fall through and let stb_image
		// reject. (Compressed-image path is the common case for glTF.)
	}

	int w = 0, h = 0, channels = 0;
	stbi_uc* pixels = stbi_load_from_memory(src.bytes.data(),
	                                        static_cast<int>(src.bytes.size()),
	                                        &w, &h, &channels, 4);  // force RGBA
	if (!pixels) {
		if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "stbi failed";
		return false;
	}

	out.width  = w;
	out.height = h;
	out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
	stbi_image_free(pixels);
	return true;
}

}  // namespace civcraft::voxel_earth

#pragma once

// JPG/PNG decode + UV sampling for the embedded glTF image.
// Backed by stb_image.h (vendored at third_party/stb/).

#include "server/voxel_earth/glb_loader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace solarium::voxel_earth {

struct Texture {
	int                  width  = 0;
	int                  height = 0;
	std::vector<uint8_t> rgba;     // row-major, 4 bytes/px, top-left origin

	bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }

	// Bilinear sample at normalized (u, v). UVs are clamped to [0, 1].
	// `out` receives R, G, B, A in [0, 255].
	void sample(float u, float v, uint8_t out[4]) const;
};

// Decode `src.bytes` (compressed JPG/PNG) into `out`. Already-decoded RGBA8
// (format == "rgba8") is passed through untouched. Returns false on failure.
bool decode_image(const EmbeddedImage& src, Texture& out, std::string* error = nullptr);

}  // namespace solarium::voxel_earth

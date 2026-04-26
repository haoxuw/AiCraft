#pragma once

// GLB → in-memory Mesh, via Assimp. Handles KHR_draco_mesh_compression
// transparently (Assimp 5.x links draco).
//
// Phase 2 of the voxel_earth pipeline. Coordinates are kept as-is from the
// GLB (ECEF-ish for Google Photorealistic 3D Tiles). The ECEF→ENU rotate +
// per-root bake is a separate stage (rotate.h, next).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace solarium::voxel_earth {

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };

struct Mesh {
	std::vector<Vec3>     positions;
	std::vector<Vec3>     normals;   // may be empty
	std::vector<Vec2>     uvs;       // may be empty
	std::vector<uint32_t> indices;   // 3 per triangle (post-Triangulate)
	uint32_t              material_index = 0;
};

// First embedded image, raw compressed bytes (JPG/PNG). Decoded to RGBA in a
// later stage when we wire stb_image / libjpeg in for the voxelizer.
struct EmbeddedImage {
	std::string format;            // e.g. "jpg", "png" (Assimp's mFormatHint)
	std::vector<uint8_t> bytes;    // empty if the GLB has no embedded image
};

struct Glb {
	std::vector<Mesh>      meshes;
	EmbeddedImage          texture0;
	std::array<float, 3>   root_translation { 0.0f, 0.0f, 0.0f };
	std::array<float, 16>  root_matrix      {
		1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1   // column-major identity
	};
	bool                   uses_draco = false;  // detected from the original .glb's JSON chunk
};

// Loads `path` and fills `out`. Returns false on failure (and writes a message
// to *error if non-null). Uses Triangulate + JoinIdenticalVertices; does NOT
// pretransform vertices — root node TRS is left for the next stage to apply.
bool load_glb(const std::string& path, Glb& out, std::string* error = nullptr);

}  // namespace solarium::voxel_earth

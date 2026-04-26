#include "server/voxel_earth/glb_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace civcraft::voxel_earth {

namespace {

// Peek the GLB JSON chunk and look for KHR_draco_mesh_compression. Assimp
// will already have decoded it by the time we read the scene; this only
// records whether decompression happened (useful for smoke / perf logs).
bool detect_draco(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	uint32_t header[3] = {0, 0, 0};
	f.read(reinterpret_cast<char*>(header), sizeof(header));
	if (!f || header[0] != 0x46546c67u /* 'glTF' */ || header[1] != 2u) return false;

	uint32_t chunk_len = 0, chunk_type = 0;
	f.read(reinterpret_cast<char*>(&chunk_len), 4);
	f.read(reinterpret_cast<char*>(&chunk_type), 4);
	if (!f || chunk_type != 0x4e4f534au /* 'JSON' */) return false;

	std::string json(chunk_len, '\0');
	f.read(json.data(), chunk_len);
	return json.find("KHR_draco_mesh_compression") != std::string::npos;
}

void copy_root_node(const aiNode* root, Glb& out) {
	if (!root) return;
	const aiMatrix4x4& m = root->mTransformation;
	// aiMatrix4x4 is row-major in memory; flip to column-major for output.
	out.root_matrix = {
		m.a1, m.b1, m.c1, m.d1,
		m.a2, m.b2, m.c2, m.d2,
		m.a3, m.b3, m.c3, m.d3,
		m.a4, m.b4, m.c4, m.d4,
	};
	out.root_translation = { m.a4, m.b4, m.c4 };
}

void copy_mesh(const aiMesh* in, Mesh& out) {
	out.positions.resize(in->mNumVertices);
	for (unsigned i = 0; i < in->mNumVertices; ++i) {
		const aiVector3D& p = in->mVertices[i];
		out.positions[i] = { p.x, p.y, p.z };
	}
	if (in->HasNormals()) {
		out.normals.resize(in->mNumVertices);
		for (unsigned i = 0; i < in->mNumVertices; ++i) {
			const aiVector3D& n = in->mNormals[i];
			out.normals[i] = { n.x, n.y, n.z };
		}
	}
	if (in->HasTextureCoords(0)) {
		out.uvs.resize(in->mNumVertices);
		for (unsigned i = 0; i < in->mNumVertices; ++i) {
			const aiVector3D& t = in->mTextureCoords[0][i];
			out.uvs[i] = { t.x, t.y };
		}
	}
	out.indices.reserve(in->mNumFaces * 3);
	for (unsigned i = 0; i < in->mNumFaces; ++i) {
		const aiFace& f = in->mFaces[i];
		if (f.mNumIndices != 3) continue;  // post-Triangulate → always 3
		out.indices.push_back(f.mIndices[0]);
		out.indices.push_back(f.mIndices[1]);
		out.indices.push_back(f.mIndices[2]);
	}
	out.material_index = in->mMaterialIndex;
}

void copy_texture0(const aiScene* scene, EmbeddedImage& out) {
	if (scene->mNumTextures == 0 || scene->mTextures == nullptr) return;
	const aiTexture* t = scene->mTextures[0];
	if (!t) return;
	out.format = t->achFormatHint ? std::string(t->achFormatHint) : std::string();
	if (t->mHeight == 0) {
		// Compressed (JPG/PNG/etc.): mWidth is the byte count, pcData the raw bytes.
		const uint8_t* p = reinterpret_cast<const uint8_t*>(t->pcData);
		out.bytes.assign(p, p + t->mWidth);
	} else {
		// Uncompressed BGRA8 — convert to RGBA.
		const size_t n = static_cast<size_t>(t->mWidth) * t->mHeight;
		out.bytes.resize(n * 4);
		for (size_t i = 0; i < n; ++i) {
			const aiTexel& px = t->pcData[i];
			out.bytes[i * 4 + 0] = px.r;
			out.bytes[i * 4 + 1] = px.g;
			out.bytes[i * 4 + 2] = px.b;
			out.bytes[i * 4 + 3] = px.a;
		}
		out.format = "rgba8";
	}
}

}  // namespace

bool load_glb(const std::string& path, Glb& out, std::string* error) {
	Assimp::Importer importer;
	const unsigned flags = aiProcess_Triangulate
	                     | aiProcess_JoinIdenticalVertices
	                     | aiProcess_SortByPType;
	const aiScene* scene = importer.ReadFile(path, flags);
	if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
		if (error) *error = importer.GetErrorString();
		return false;
	}

	out.meshes.clear();
	out.meshes.resize(scene->mNumMeshes);
	for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
		copy_mesh(scene->mMeshes[i], out.meshes[i]);
	}
	copy_root_node(scene->mRootNode, out);
	copy_texture0(scene, out.texture0);
	out.uses_draco = detect_draco(path);
	return true;
}

}  // namespace civcraft::voxel_earth

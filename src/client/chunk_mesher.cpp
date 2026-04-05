#include "client/chunk_mesher.h"

namespace agentica {

void ChunkMesh::upload(const std::vector<ChunkVertex>& vertices) {
	vertexCount = (int)vertices.size();
	if (vertexCount == 0) return;
	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
	}
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ChunkVertex),
		vertices.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, position));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, color));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, normal));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, ao));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, shade));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, alpha));
	glEnableVertexAttribArray(5);
	glBindVertexArray(0);
}

void ChunkMesh::draw() const {
	if (vertexCount == 0) return;
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void ChunkMesh::destroy() {
	if (vbo) glDeleteBuffers(1, &vbo);
	if (vao) glDeleteVertexArrays(1, &vao);
	vao = vbo = 0;
	vertexCount = 0;
}

float ChunkMesher::computeAO(bool side1, bool side2, bool corner) {
	if (side1 && side2) return 0.1f;
	int count = (int)side1 + (int)side2 + (int)corner;
	constexpr float levels[] = {1.0f, 0.70f, 0.45f, 0.1f};
	return levels[count];
}

// Padded volume: 18x18x18 (chunk + 1 block border on each side)
// This eliminates all per-block getBlock()/getChunk()/mutex calls during meshing.
static constexpr int PAD = 1;
static constexpr int PADDED = CHUNK_SIZE + 2 * PAD; // 18

static inline int padIdx(int x, int y, int z) {
	return (y + PAD) * PADDED * PADDED + (z + PAD) * PADDED + (x + PAD);
}

void ChunkMesher::fillPaddedVolume(ChunkSource& world, ChunkPos cpos) {
	// Fetch center + all 26 neighbors in a single mutex lock
	auto neighborhood = world.getChunkNeighborhood(cpos);
	// Index: (dy+1)*9 + (dz+1)*3 + (dx+1), center = index 13
	Chunk* center = neighborhood[13];
	if (!center) return;

	// Initialize to air
	m_padded.fill(BLOCK_AIR);

	// Copy center chunk (local 0..15 → padded 1..16)
	for (int y = 0; y < CHUNK_SIZE; y++)
		for (int z = 0; z < CHUNK_SIZE; z++)
			for (int x = 0; x < CHUNK_SIZE; x++)
				m_padded[padIdx(x, y, z)] = center->get(x, y, z);

	// Copy 1-block border from each loaded neighbor
	for (int dy = -1; dy <= 1; dy++)
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dy == 0 && dz == 0) continue;
				Chunk* nc = neighborhood[(dy+1)*9 + (dz+1)*3 + (dx+1)];
				if (!nc) continue;

				// Determine which border region this neighbor contributes
				int xmin = (dx == -1) ? -1 : (dx == 1) ? CHUNK_SIZE : 0;
				int xmax = (dx == -1) ? -1 : (dx == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;
				int ymin = (dy == -1) ? -1 : (dy == 1) ? CHUNK_SIZE : 0;
				int ymax = (dy == -1) ? -1 : (dy == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;
				int zmin = (dz == -1) ? -1 : (dz == 1) ? CHUNK_SIZE : 0;
				int zmax = (dz == -1) ? -1 : (dz == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;

				for (int y = ymin; y <= ymax; y++)
					for (int z = zmin; z <= zmax; z++)
						for (int x = xmin; x <= xmax; x++) {
							int sx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							int sy = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							int sz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							m_padded[padIdx(x, y, z)] = nc->get(sx, sy, sz);
						}
			}
}

std::vector<ChunkVertex> ChunkMesher::buildMesh(ChunkSource& world, ChunkPos cpos) {
	std::vector<ChunkVertex> verts;
	verts.reserve(4096);

	Chunk* chunk = world.getChunkIfLoaded(cpos);
	if (!chunk) return verts;

	// Fill padded volume with cached block data (eliminates per-block mutex/hash lookups)
	fillPaddedVolume(world, cpos);

	const BlockRegistry& reg = world.blockRegistry();
	int ox = cpos.x * CHUNK_SIZE;
	int oy = cpos.y * CHUNK_SIZE;
	int oz = cpos.z * CHUNK_SIZE;

	// Local block access via padded cache - no mutex, no hash lookup
	auto cachedBlock = [&](int lx, int ly, int lz) -> BlockId {
		int idx = padIdx(lx, ly, lz);
		if (idx < 0 || idx >= (int)m_padded.size()) return BLOCK_AIR;
		return m_padded[idx];
	};

	// Emit all 6 faces of an axis-aligned box (no neighbor culling).
	// Used for non-cube mesh types (stairs, doors) where partial geometry
	// doesn't align with the 1x1x1 cell assumed by the cube face-cull check.
	auto emitBox = [&](float x0, float y0, float z0,
	                   float x1, float y1, float z1,
	                   glm::vec3 cTop, glm::vec3 cSide) {
		constexpr glm::vec3 norms[6] = {
			{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
		};
		// 4 verts per face, counterclockwise when viewed from outside
		float vs[6][4][3] = {
			{{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}}, // +X
			{{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}}, // -X
			{{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}, // +Y (top)
			{{x0,y0,z1},{x0,y0,z0},{x1,y0,z0},{x1,y0,z1}}, // -Y (bottom)
			{{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},{x0,y0,z1}}, // +Z
			{{x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0}}, // -Z
		};
		for (int f = 0; f < 6; f++) {
			glm::vec3 col = (f == 2) ? cTop : cSide;
			float shade = BLOCK_FACE_SHADE[f];
			auto emit = [&](int i) {
				auto& v = vs[f][i];
				verts.push_back({{v[0],v[1],v[2]}, col, norms[f], 1.0f, shade, 1.0f});
			};
			// Two triangles: 0,1,2 and 0,2,3
			emit(0); emit(1); emit(2);
			emit(0); emit(2); emit(3);
		}
	};

	for (int ly = 0; ly < CHUNK_SIZE; ly++)
	for (int lz = 0; lz < CHUNK_SIZE; lz++)
	for (int lx = 0; lx < CHUNK_SIZE; lx++) {
		BlockId block = cachedBlock(lx, ly, lz);
		if (block == BLOCK_AIR) continue;

		const BlockDef& bdef = reg.get(block);
		// Skip air-like blocks (non-solid cubes like water, leaves)
		// but DO render non-solid blocks with special mesh types (open doors).
		if (!bdef.solid && bdef.mesh_type == MeshType::Cube) continue;

		int wx = ox + lx, wy = oy + ly, wz = oz + lz;
		float fx = (float)wx, fy = (float)wy, fz = (float)wz;

		// ── Non-cube mesh types ──────────────────────────────────
		if (bdef.mesh_type != MeshType::Cube) {
			if (bdef.mesh_type == MeshType::Stair) {
				// Bottom slab: full width/depth, bottom half height
				emitBox(fx, fy,       fz, fx+1, fy+0.5f, fz+1,
				        bdef.color_top, bdef.color_side);
				// Back step: full width, top half height, back half depth
				emitBox(fx, fy+0.5f, fz+0.5f, fx+1, fy+1, fz+1,
				        bdef.color_top, bdef.color_side);
			} else if (bdef.mesh_type == MeshType::Door) {
				// Closed door: thin panel flush with -Z face of the cell
				emitBox(fx, fy, fz, fx+1, fy+1, fz+0.1f,
				        bdef.color_top, bdef.color_side);
			} else if (bdef.mesh_type == MeshType::DoorOpen) {
				// Open door: thin panel flush with -X face of the cell
				emitBox(fx, fy, fz, fx+0.1f, fy+1, fz+1,
				        bdef.color_top, bdef.color_side);
			}
			continue; // skip the cube face loop below
		}

		for (int face = 0; face < 6; face++) {
			int nlx = lx + FACE_DIRS[face].x;
			int nly = ly + FACE_DIRS[face].y;
			int nlz = lz + FACE_DIRS[face].z;

			BlockId neighbor = cachedBlock(nlx, nly, nlz);
			const BlockDef& ndef = reg.get(neighbor);
			if (ndef.solid) continue;

			glm::vec3 color;
			if (face == 2)      color = bdef.color_top;
			else if (face == 3) color = bdef.color_bottom;
			else                color = bdef.color_side;

			glm::vec3 normal = glm::vec3(FACE_DIRS[face]);
			float shade = BLOCK_FACE_SHADE[face];

			auto solid = [&](int x, int y, int z) -> bool {
				return reg.get(cachedBlock(x, y, z)).solid;
			};

			glm::vec3 v[4];
			float ao[4];

			switch (face) {
			case 0: // +X
				v[0]={wx+1.f,wy+0.f,wz+0.f}; v[1]={wx+1.f,wy+1.f,wz+0.f};
				v[2]={wx+1.f,wy+1.f,wz+1.f}; v[3]={wx+1.f,wy+0.f,wz+1.f};
				ao[0]=computeAO(solid(lx+1,ly-1,lz),solid(lx+1,ly,lz-1),solid(lx+1,ly-1,lz-1));
				ao[1]=computeAO(solid(lx+1,ly+1,lz),solid(lx+1,ly,lz-1),solid(lx+1,ly+1,lz-1));
				ao[2]=computeAO(solid(lx+1,ly+1,lz),solid(lx+1,ly,lz+1),solid(lx+1,ly+1,lz+1));
				ao[3]=computeAO(solid(lx+1,ly-1,lz),solid(lx+1,ly,lz+1),solid(lx+1,ly-1,lz+1));
				break;
			case 1: // -X
				v[0]={wx+0.f,wy+0.f,wz+1.f}; v[1]={wx+0.f,wy+1.f,wz+1.f};
				v[2]={wx+0.f,wy+1.f,wz+0.f}; v[3]={wx+0.f,wy+0.f,wz+0.f};
				ao[0]=computeAO(solid(lx-1,ly-1,lz),solid(lx-1,ly,lz+1),solid(lx-1,ly-1,lz+1));
				ao[1]=computeAO(solid(lx-1,ly+1,lz),solid(lx-1,ly,lz+1),solid(lx-1,ly+1,lz+1));
				ao[2]=computeAO(solid(lx-1,ly+1,lz),solid(lx-1,ly,lz-1),solid(lx-1,ly+1,lz-1));
				ao[3]=computeAO(solid(lx-1,ly-1,lz),solid(lx-1,ly,lz-1),solid(lx-1,ly-1,lz-1));
				break;
			case 2: // +Y
				v[0]={wx+0.f,wy+1.f,wz+0.f}; v[1]={wx+0.f,wy+1.f,wz+1.f};
				v[2]={wx+1.f,wy+1.f,wz+1.f}; v[3]={wx+1.f,wy+1.f,wz+0.f};
				ao[0]=computeAO(solid(lx-1,ly+1,lz),solid(lx,ly+1,lz-1),solid(lx-1,ly+1,lz-1));
				ao[1]=computeAO(solid(lx-1,ly+1,lz),solid(lx,ly+1,lz+1),solid(lx-1,ly+1,lz+1));
				ao[2]=computeAO(solid(lx+1,ly+1,lz),solid(lx,ly+1,lz+1),solid(lx+1,ly+1,lz+1));
				ao[3]=computeAO(solid(lx+1,ly+1,lz),solid(lx,ly+1,lz-1),solid(lx+1,ly+1,lz-1));
				break;
			case 3: // -Y
				v[0]={wx+0.f,wy+0.f,wz+1.f}; v[1]={wx+0.f,wy+0.f,wz+0.f};
				v[2]={wx+1.f,wy+0.f,wz+0.f}; v[3]={wx+1.f,wy+0.f,wz+1.f};
				ao[0]=computeAO(solid(lx-1,ly-1,lz),solid(lx,ly-1,lz+1),solid(lx-1,ly-1,lz+1));
				ao[1]=computeAO(solid(lx-1,ly-1,lz),solid(lx,ly-1,lz-1),solid(lx-1,ly-1,lz-1));
				ao[2]=computeAO(solid(lx+1,ly-1,lz),solid(lx,ly-1,lz-1),solid(lx+1,ly-1,lz-1));
				ao[3]=computeAO(solid(lx+1,ly-1,lz),solid(lx,ly-1,lz+1),solid(lx+1,ly-1,lz+1));
				break;
			case 4: // +Z
				v[0]={wx+1.f,wy+0.f,wz+1.f}; v[1]={wx+1.f,wy+1.f,wz+1.f};
				v[2]={wx+0.f,wy+1.f,wz+1.f}; v[3]={wx+0.f,wy+0.f,wz+1.f};
				ao[0]=computeAO(solid(lx,ly-1,lz+1),solid(lx+1,ly,lz+1),solid(lx+1,ly-1,lz+1));
				ao[1]=computeAO(solid(lx,ly+1,lz+1),solid(lx+1,ly,lz+1),solid(lx+1,ly+1,lz+1));
				ao[2]=computeAO(solid(lx,ly+1,lz+1),solid(lx-1,ly,lz+1),solid(lx-1,ly+1,lz+1));
				ao[3]=computeAO(solid(lx,ly-1,lz+1),solid(lx-1,ly,lz+1),solid(lx-1,ly-1,lz+1));
				break;
			case 5: // -Z
				v[0]={wx+0.f,wy+0.f,wz+0.f}; v[1]={wx+0.f,wy+1.f,wz+0.f};
				v[2]={wx+1.f,wy+1.f,wz+0.f}; v[3]={wx+1.f,wy+0.f,wz+0.f};
				ao[0]=computeAO(solid(lx,ly-1,lz-1),solid(lx-1,ly,lz-1),solid(lx-1,ly-1,lz-1));
				ao[1]=computeAO(solid(lx,ly+1,lz-1),solid(lx-1,ly,lz-1),solid(lx-1,ly+1,lz-1));
				ao[2]=computeAO(solid(lx,ly+1,lz-1),solid(lx+1,ly,lz-1),solid(lx+1,ly+1,lz-1));
				ao[3]=computeAO(solid(lx,ly-1,lz-1),solid(lx+1,ly,lz-1),solid(lx+1,ly-1,lz-1));
				break;
			}

			// All blocks are fully opaque
			auto emit = [&](int i) { verts.push_back({v[i], color, normal, ao[i], shade, 1.0f}); };
			if (ao[0] + ao[2] > ao[1] + ao[3]) {
				emit(0); emit(1); emit(2); emit(0); emit(2); emit(3);
			} else {
				emit(1); emit(2); emit(3); emit(1); emit(3); emit(0);
			}
		}
	}
	return verts;
}

} // namespace agentica

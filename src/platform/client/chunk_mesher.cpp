#include "client/chunk_mesher.h"

namespace civcraft {

float ChunkMesher::computeAO(bool side1, bool side2, bool corner) {
	if (side1 && side2) return 0.1f;
	int count = (int)side1 + (int)side2 + (int)corner;
	constexpr float levels[] = {1.0f, 0.70f, 0.45f, 0.1f};
	return levels[count];
}

// 18³ padded volume eliminates per-block getBlock()/getChunk()/mutex during meshing.
static constexpr int PAD = 1;
static constexpr int PADDED = CHUNK_SIZE + 2 * PAD; // 18

static inline int padIdx(int x, int y, int z) {
	return (y + PAD) * PADDED * PADDED + (z + PAD) * PADDED + (x + PAD);
}

void ChunkMesher::fillPaddedVolume(ChunkSource& world, ChunkPos cpos) {
	// Fetch center + 26 neighbors in one mutex lock. Index = (dy+1)*9+(dz+1)*3+(dx+1); center=13.
	auto neighborhood = world.getChunkNeighborhood(cpos);
	Chunk* center = neighborhood[13];
	if (!center) return;

	m_padded.fill(BLOCK_AIR);

	// Copy center chunk (local 0..15 → padded 1..16).
	for (int y = 0; y < CHUNK_SIZE; y++)
		for (int z = 0; z < CHUNK_SIZE; z++)
			for (int x = 0; x < CHUNK_SIZE; x++)
				m_padded[padIdx(x, y, z)] = center->get(x, y, z);

	for (int dy = -1; dy <= 1; dy++)
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dy == 0 && dz == 0) continue;
				Chunk* nc = neighborhood[(dy+1)*9 + (dz+1)*3 + (dx+1)];
				if (!nc) continue;

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

std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
ChunkMesher::buildMesh(ChunkSource& world, ChunkPos cpos) {
	std::vector<ChunkVertex> verts;      // opaque
	std::vector<ChunkVertex> tVerts;     // transparent
	verts.reserve(4096);
	tVerts.reserve(512);

	Chunk* chunk = world.getChunkIfLoaded(cpos);
	if (!chunk) return {verts, tVerts};

	fillPaddedVolume(world, cpos);

	const BlockRegistry& reg = world.blockRegistry();
	int ox = cpos.x * CHUNK_SIZE;
	int oy = cpos.y * CHUNK_SIZE;
	int oz = cpos.z * CHUNK_SIZE;

	// Padded-cache block access — no mutex, no hash lookup.
	auto cachedBlock = [&](int lx, int ly, int lz) -> BlockId {
		int idx = padIdx(lx, ly, lz);
		if (idx < 0 || idx >= (int)m_padded.size()) return BLOCK_AIR;
		return m_padded[idx];
	};

	// 6-face AABB emit, no neighbor culling. Used for non-cube meshes (stairs, doors)
	// where partial geometry breaks the 1x1x1-cell face-cull assumption.
	auto emitBox = [&](float x0, float y0, float z0,
	                   float x1, float y1, float z1,
	                   glm::vec3 cTop, glm::vec3 cSide, float alpha) {
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
		auto& dst = (alpha < 1.0f) ? tVerts : verts;
		for (int f = 0; f < 6; f++) {
			glm::vec3 col = (f == 2) ? cTop : cSide;
			float shade = BLOCK_FACE_SHADE[f];
			auto emit = [&](int i) {
				auto& v = vs[f][i];
				dst.push_back({{v[0],v[1],v[2]}, col, norms[f], 1.0f, shade, alpha, 0.0f});
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
		// Skip non-solid cubes (water, leaves); non-cube meshes (open doors) still render.
		if (!bdef.solid && bdef.mesh_type == MeshType::Cube) continue;

		int wx = ox + lx, wy = oy + ly, wz = oz + lz;
		float fx = (float)wx, fy = (float)wy, fz = (float)wz;

		// ── Non-cube mesh types ──────────────────────────────────
		if (bdef.mesh_type != MeshType::Cube) {
			float a = bdef.transparent ? 0.75f : 1.0f;
			if (bdef.mesh_type == MeshType::Stair) {
				// 10 exterior faces of the stair L-shape. param2 bits 0-1 (FourDir) rotate
				// 90° around Y: 0=+Z, 1=+X, 2=-Z, 3=-X (rise direction). Local (u,v) ∈ [0,1]²
				// → world (x,z); positive Jacobian preserves CCW winding across all rotations.
				glm::vec3 ct = bdef.color_top, cs = bdef.color_side;
				auto& dst = (a < 1.0f) ? tVerts : verts;
				float y0=fy, y5=fy+0.5f, y1=fy+1;
				uint8_t p2 = chunk->getParam2(lx, ly, lz) & 0x3;

				// Local (u,v) → world (x,z). v=0 tread/near, v=1 back/far.
				auto wx = [&](float u, float v) -> float {
					switch (p2) {
					default:
					case 0: return fx + u;
					case 1: return fx + 1.f - v;
					case 2: return fx + 1.f - u;
					case 3: return fx + v;
					}
				};
				auto wz = [&](float u, float v) -> float {
					switch (p2) {
					default:
					case 0: return fz + v;
					case 1: return fz + u;
					case 2: return fz + 1.f - v;
					case 3: return fz + 1.f - u;
					}
				};
				// Rotate horizontal normal matching the u/v 90° CCW: (nx,nz) → (-nz,nx).
				auto rn = [&](float nx, float nz) -> glm::vec3 {
					switch (p2) {
					default:
					case 0: return { nx,  0.f,  nz};
					case 1: return {-nz,  0.f,  nx};
					case 2: return {-nx,  0.f, -nz};
					case 3: return { nz,  0.f, -nx};
					}
				};
				// Emit one quad (2 tris, CCW outward).
				auto eq = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3,
				              glm::vec3 n, glm::vec3 col) {
					float sh;
					if      (n.x > 0.5f)  sh = BLOCK_FACE_SHADE[0];
					else if (n.x < -0.5f) sh = BLOCK_FACE_SHADE[1];
					else if (n.y > 0.5f)  sh = BLOCK_FACE_SHADE[2];
					else if (n.y < -0.5f) sh = BLOCK_FACE_SHADE[3];
					else if (n.z > 0.5f)  sh = BLOCK_FACE_SHADE[4];
					else                   sh = BLOCK_FACE_SHADE[5];
					dst.push_back({v0,col,n,1.f,sh,a,0.f}); dst.push_back({v1,col,n,1.f,sh,a,0.f});
					dst.push_back({v2,col,n,1.f,sh,a,0.f}); dst.push_back({v0,col,n,1.f,sh,a,0.f});
					dst.push_back({v2,col,n,1.f,sh,a,0.f}); dst.push_back({v3,col,n,1.f,sh,a,0.f});
				};
				// 10 faces in (u,v). v=0 near/tread, v=1 far/back.
				eq({wx(0,1),y0,wz(0,1)},{wx(0,0),y0,wz(0,0)},{wx(1,0),y0,wz(1,0)},{wx(1,1),y0,wz(1,1)}, {0,-1,0},        cs); // bottom
				eq({wx(0,0),y0,wz(0,0)},{wx(0,0),y5,wz(0,0)},{wx(1,0),y5,wz(1,0)},{wx(1,0),y0,wz(1,0)}, rn(0,-1),         cs); // near face (half-height)
				eq({wx(0,0),y5,wz(0,0)},{wx(0,.5f),y5,wz(0,.5f)},{wx(1,.5f),y5,wz(1,.5f)},{wx(1,0),y5,wz(1,0)}, {0,1,0}, ct); // tread
				eq({wx(0,.5f),y5,wz(0,.5f)},{wx(0,.5f),y1,wz(0,.5f)},{wx(1,.5f),y1,wz(1,.5f)},{wx(1,.5f),y5,wz(1,.5f)}, rn(0,-1), cs); // riser
				eq({wx(0,.5f),y1,wz(0,.5f)},{wx(0,1),y1,wz(0,1)},{wx(1,1),y1,wz(1,1)},{wx(1,.5f),y1,wz(1,.5f)}, {0,1,0}, ct); // top (back half)
				eq({wx(1,1),y0,wz(1,1)},{wx(1,1),y1,wz(1,1)},{wx(0,1),y1,wz(0,1)},{wx(0,1),y0,wz(0,1)}, rn(0,1),           cs); // back face (full-height)
				eq({wx(0,1),y0,wz(0,1)},{wx(0,1),y5,wz(0,1)},{wx(0,0),y5,wz(0,0)},{wx(0,0),y0,wz(0,0)}, rn(-1,0),          cs); // left lower
				eq({wx(0,1),y5,wz(0,1)},{wx(0,1),y1,wz(0,1)},{wx(0,.5f),y1,wz(0,.5f)},{wx(0,.5f),y5,wz(0,.5f)}, rn(-1,0),  cs); // left upper
				eq({wx(1,0),y0,wz(1,0)},{wx(1,0),y5,wz(1,0)},{wx(1,1),y5,wz(1,1)},{wx(1,1),y0,wz(1,1)}, rn(1,0),           cs); // right lower
				eq({wx(1,.5f),y5,wz(1,.5f)},{wx(1,.5f),y1,wz(1,.5f)},{wx(1,1),y1,wz(1,1)},{wx(1,1),y5,wz(1,1)}, rn(1,0),   cs); // right upper
			} else if (bdef.mesh_type == MeshType::Door) {
				// Closed: thin panel flush with -Z face.
				emitBox(fx, fy, fz, fx+1, fy+1, fz+0.1f,
				        bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::DoorOpen) {
				// Open: thin panel on ±X face (hinge = param2 bit 2).
				uint8_t p2 = chunk->getParam2(lx, ly, lz);
				bool hingeRight = (p2 >> 2) & 1;
				if (hingeRight)
					emitBox(fx+0.9f, fy, fz, fx+1, fy+1, fz+1, bdef.color_top, bdef.color_side, a);
				else
					emitBox(fx, fy, fz, fx+0.1f, fy+1, fz+1, bdef.color_top, bdef.color_side, a);
			}
			continue;
		}

		for (int face = 0; face < 6; face++) {
			int nlx = lx + FACE_DIRS[face].x;
			int nly = ly + FACE_DIRS[face].y;
			int nlz = lz + FACE_DIRS[face].z;

			BlockId neighbor = cachedBlock(nlx, nly, nlz);
			const BlockDef& ndef = reg.get(neighbor);
			// Cull face if opaque-solid neighbor. Y-faces cull against any solid; X/Z-faces
			// only against full-cube solids (floor sides remain visible next to stairs).
			bool isYFace = (face == 2 || face == 3);
			if (ndef.solid && !ndef.transparent && (isYFace || ndef.mesh_type == MeshType::Cube)) continue;

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

			float alpha = bdef.transparent ? 0.42f : 1.0f;
			float glow  = bdef.surface_glow ? 1.0f : 0.0f;
			auto& dst = bdef.transparent ? tVerts : verts;
			auto emit = [&](int i) { dst.push_back({v[i], color, normal, ao[i], shade, alpha, glow}); };
			if (ao[0] + ao[2] > ao[1] + ao[3]) {
				emit(0); emit(1); emit(2); emit(0); emit(2); emit(3);
			} else {
				emit(1); emit(2); emit(3); emit(1); emit(3); emit(0);
			}
		}
	}
	return {verts, tVerts};
}

} // namespace civcraft

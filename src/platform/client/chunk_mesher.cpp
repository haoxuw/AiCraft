#include "client/chunk_mesher.h"

#include <cmath>

namespace solarium {

// Stable per-blade hash. Used to seed the random position / angle / height
// of each blade within a grass cell so blades appear scattered but stay
// deterministic between mesh rebuilds (no flicker on chunk reload).
static inline float bladeHash(int x, int z, int k) {
	unsigned int n = (unsigned int)(x * 73856093)
	              ^ (unsigned int)(z * 19349663)
	              ^ (unsigned int)(k * 83492791);
	n = (n << 13) ^ n;
	return (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff)
	       / 2147483647.0f;
}

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

// Fills `out` from the live neighborhood. Expects a consistent 3x3x3 snapshot
// from ChunkSource (locking is the source's responsibility). Returns false if
// the center chunk isn't loaded — caller should skip meshing.
static bool fillSnapshot(ChunkSource& world, ChunkPos cpos,
                         ChunkMesher::PaddedSnapshot& out) {
	auto neighborhood = world.getChunkNeighborhood(cpos);
	Chunk* center = neighborhood[13];
	if (!center) return false;

	out.blocks.fill(BLOCK_AIR);
	out.param2.fill(0);
	out.appearance.fill(0);

	for (int y = 0; y < CHUNK_SIZE; y++)
		for (int z = 0; z < CHUNK_SIZE; z++)
			for (int x = 0; x < CHUNK_SIZE; x++) {
				int i = padIdx(x, y, z);
				out.blocks[i]     = center->get(x, y, z);
				out.param2[i]     = center->getParam2(x, y, z);
				out.appearance[i] = center->getAppearance(x, y, z);
			}

	for (int dy = -1; dy <= 1; dy++)
		for (int dz = -1; dz <= 1; dz++)
			for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dy == 0 && dz == 0) continue;
				Chunk* nc = neighborhood[(dy+1)*9 + (dz+1)*3 + (dx+1)];

				int xmin = (dx == -1) ? -1 : (dx == 1) ? CHUNK_SIZE : 0;
				int xmax = (dx == -1) ? -1 : (dx == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;
				int ymin = (dy == -1) ? -1 : (dy == 1) ? CHUNK_SIZE : 0;
				int ymax = (dy == -1) ? -1 : (dy == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;
				int zmin = (dz == -1) ? -1 : (dz == 1) ? CHUNK_SIZE : 0;
				int zmax = (dz == -1) ? -1 : (dz == 1) ? CHUNK_SIZE : CHUNK_SIZE - 1;

				// Unloaded neighbour: paint the ring cells with the default
				// fill for that neighbour's chunk-y so face culling matches
				// "absent S_CHUNK = AIR above, DIRT below". Without this, the
				// top face of every loaded dirt chunk emits against the
				// unloaded chunk above, drawing useless geometry; or worse,
				// the bottom of every loaded surface chunk emits a
				// dirt-coloured face into the void.
				if (!nc) {
					const BlockId fill = world.defaultBlock(cpos.y + dy);
					for (int y = ymin; y <= ymax; y++)
						for (int z = zmin; z <= zmax; z++)
							for (int x = xmin; x <= xmax; x++)
								out.blocks[padIdx(x, y, z)] = fill;
					continue;
				}

				for (int y = ymin; y <= ymax; y++)
					for (int z = zmin; z <= zmax; z++)
						for (int x = xmin; x <= xmax; x++) {
							int sx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							int sy = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							int sz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
							out.blocks[padIdx(x, y, z)] = nc->get(sx, sy, sz);
						}
			}
	return true;
}

bool ChunkMesher::snapshotPadded(ChunkSource& world, ChunkPos cpos,
                                 PaddedSnapshot& out) {
	return fillSnapshot(world, cpos, out);
}

void ChunkMesher::fillPaddedVolume(ChunkSource& world, ChunkPos cpos) {
	fillSnapshot(world, cpos, m_padded);
}

std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
ChunkMesher::buildMesh(ChunkSource& world, ChunkPos cpos) {
	Chunk* chunk = world.getChunkIfLoaded(cpos);
	if (!chunk) return {{}, {}};

	// I8 cull: skip snapshot+meshing when no face from this chunk would emit.
	// Lite uniformity means every cell is identical, so the per-cell face test
	// reduces to a uniform yes/no per direction.
	if (chunk->isLite()) {
		const auto& reg = world.blockRegistry();
		const BlockDef& cdef = reg.get(chunk->liteBid());

		// Non-cube blocks (wheat, doors, stairs) emit special geometry every
		// cell, regardless of neighbors — never safe to cull.
		if (cdef.mesh_type == MeshType::Cube) {
			if (!cdef.solid) {
				// Non-solid (air): no faces emitted from this chunk ever.
				return {{}, {}};
			}
			// Solid cube. A face emits iff the neighbor isn't fully occluding.
			// Mirror the per-face cull rule used by the inner loop (line 262).
			auto neigh = world.getChunkNeighborhood(cpos);
			const Chunk* faces[6] = {
				neigh[(0+1)*9 + (0+1)*3 + (-1+1)],  // -X (face 0)
				neigh[(0+1)*9 + (0+1)*3 + ( 1+1)],  // +X (face 1)
				neigh[(-1+1)*9 + (0+1)*3 + (0+1)],  // -Y (face 2)
				neigh[( 1+1)*9 + (0+1)*3 + (0+1)],  // +Y (face 3)
				neigh[(0+1)*9 + (-1+1)*3 + (0+1)],  // -Z (face 4)
				neigh[(0+1)*9 + ( 1+1)*3 + (0+1)],  // +Z (face 5)
			};
			bool allCulled = true;
			for (int f = 0; f < 6 && allCulled; ++f) {
				const Chunk* nc = faces[f];
				if (!nc || !nc->isLite()) { allCulled = false; break; }
				const BlockDef& ndef = reg.get(nc->liteBid());
				bool isYFace = (f == 2 || f == 3);
				bool occludes = ndef.solid && !ndef.transparent
				             && (isYFace || ndef.mesh_type == MeshType::Cube);
				if (!occludes) { allCulled = false; break; }
			}
			if (allCulled) return {{}, {}};
		}
	}

	if (!fillSnapshot(world, cpos, m_padded)) return {{}, {}};
	return buildMeshFromSnapshot(m_padded, cpos, world.blockRegistry());
}

std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
ChunkMesher::buildMeshFromSnapshot(const PaddedSnapshot& padded, ChunkPos cpos,
                                   const BlockRegistry& reg) {
	std::vector<ChunkVertex> verts;      // opaque
	std::vector<ChunkVertex> tVerts;     // transparent
	verts.reserve(4096);
	tVerts.reserve(512);

	int ox = cpos.x * CHUNK_SIZE;
	int oy = cpos.y * CHUNK_SIZE;
	int oz = cpos.z * CHUNK_SIZE;

	// Padded-cache block access — no mutex, no hash lookup.
	auto cachedBlock = [&](int lx, int ly, int lz) -> BlockId {
		int idx = padIdx(lx, ly, lz);
		if (idx < 0 || idx >= (int)padded.blocks.size()) return BLOCK_AIR;
		return padded.blocks[idx];
	};

	// Generic plant-mesh emitter. Used by any block with MeshType::Plant
	// (tall_grass, ferns, cattails, …). Bezier-curve blades distilled from
	// Vulkan-Grass-Rendering (shineyruan fork, shaders/grass.tese): 3 control
	// points + De Casteljau along the blade, width tapering to a tip. We emit
	// static triangles from the CPU instead of driving a tessellation shader
	// — simpler, fits the existing ChunkVertex pipeline, works from the
	// mesher's threadpool path.
	//
	// Per cell we emit BLADES tufts of SEGMENTS segments, hash-seeded so the
	// scatter is stable across mesh rebuilds (no flicker on chunk reload).
	// Every tri is emitted in both winding orders — plants have no back face.
	//
	// `rootCol` = base shade (typically color_side), `tipCol` = leaf shade
	// (typically color_top). Appearance-palette tint (if any) has already
	// been folded into both by the caller.
	auto emitPlant = [&](int wx, int wy, int wz,
	                     const glm::vec3& rootCol, const glm::vec3& tipCol,
	                     float heightScale, int bladeCount) {
		const int BLADES = bladeCount;
		constexpr int SEGMENTS = 3;
		constexpr float BASE_SHADE = 1.0f;   // plants catch sun — treat as top-lit
		constexpr glm::vec3 UP_NORMAL = {0.0f, 1.0f, 0.0f};

		for (int b = 0; b < BLADES; b++) {
			float r0 = bladeHash(wx, wz, b * 4 + 0);  // offset X
			float r1 = bladeHash(wx, wz, b * 4 + 1);  // offset Z
			float r2 = bladeHash(wx, wz, b * 4 + 2);  // angle
			float r3 = bladeHash(wx, wz, b * 4 + 3);  // height/lean

			float bx = (float)wx + 0.15f + r0 * 0.70f;
			float bz = (float)wz + 0.15f + r1 * 0.70f;
			float by = (float)wy;   // plant cell sits on its own support

			float ang    = r2 * 6.2831853f;
			float height = (0.40f + r3 * 0.45f) * heightScale;  // 0.40..0.85
			float lean   = 0.08f + r3 * 0.18f;
			float width  = 0.045f;

			glm::vec3 lead = {std::cos(ang), 0.0f, std::sin(ang)};
			glm::vec3 side = {-lead.z, 0.0f, lead.x};

			glm::vec3 p0 = {bx, by, bz};
			glm::vec3 p2 = p0 + glm::vec3(lead.x * lean, height, lead.z * lean);
			glm::vec3 mid = (p0 + p2) * 0.5f;
			// Lift midpoint so blade arcs convexly upward (Bezier bow).
			glm::vec3 p1 = mid + glm::vec3(0.0f, height * 0.18f, 0.0f);

			glm::vec3 l[SEGMENTS + 1], r[SEGMENTS + 1];
			float colorT[SEGMENTS + 1];
			for (int s = 0; s <= SEGMENTS; s++) {
				float v = (float)s / (float)SEGMENTS;
				glm::vec3 a = p0 + v * (p1 - p0);
				glm::vec3 b2 = p1 + v * (p2 - p1);
				glm::vec3 c = a + v * (b2 - a);
				float w = width * (1.0f - v);   // tip tapers to a point
				l[s] = c - side * w;
				r[s] = c + side * w;
				colorT[s] = v;
			}

			auto pushTri = [&](const glm::vec3& a, const glm::vec3& b2,
			                   const glm::vec3& c, float ta, float tb, float tc) {
				glm::vec3 ca = glm::mix(rootCol, tipCol, ta);
				glm::vec3 cb = glm::mix(rootCol, tipCol, tb);
				glm::vec3 cc = glm::mix(rootCol, tipCol, tc);
				verts.push_back({a, ca, UP_NORMAL, 1.0f, BASE_SHADE, 1.0f, 0.0f});
				verts.push_back({b2, cb, UP_NORMAL, 1.0f, BASE_SHADE, 1.0f, 0.0f});
				verts.push_back({c, cc, UP_NORMAL, 1.0f, BASE_SHADE, 1.0f, 0.0f});
			};

			for (int s = 0; s < SEGMENTS; s++) {
				const glm::vec3& lA = l[s],     rA = r[s];
				const glm::vec3& lB = l[s + 1], rB = r[s + 1];
				float cA = colorT[s], cB = colorT[s + 1];
				pushTri(lA, rA, rB, cA, cA, cB);
				pushTri(lA, rB, lB, cA, cB, cB);
				// Double-sided: flipped winding for the back face.
				pushTri(lA, rB, rA, cA, cB, cA);
				pushTri(lA, lB, rB, cA, cB, cB);
			}
		}
	};

	// 6-face AABB emit, no neighbor culling. Used for non-cube meshes (stairs, doors)
	// where partial geometry breaks the 1x1x1-cell face-cull assumption.
	auto emitBox = [&](float x0, float y0, float z0,
	                   float x1, float y1, float z1,
	                   glm::vec3 cTop, glm::vec3 cSide, float alpha) {
		constexpr glm::vec3 norms[6] = {
			{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
		};
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
		if (!bdef.solid && bdef.mesh_type == MeshType::Cube) continue;

		int wx = ox + lx, wy = oy + ly, wz = oz + lz;
		float fx = (float)wx, fy = (float)wy, fz = (float)wz;

		if (bdef.mesh_type != MeshType::Cube) {
			float a = bdef.transparent ? 0.75f : 1.0f;
			if (bdef.mesh_type == MeshType::Stair) {
				glm::vec3 ct = bdef.color_top, cs = bdef.color_side;
				auto& dst = (a < 1.0f) ? tVerts : verts;
				float y0=fy, y5=fy+0.5f, y1=fy+1;
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)] & 0x3;

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
				auto rn = [&](float nx, float nz) -> glm::vec3 {
					switch (p2) {
					default:
					case 0: return { nx,  0.f,  nz};
					case 1: return {-nz,  0.f,  nx};
					case 2: return {-nx,  0.f, -nz};
					case 3: return { nz,  0.f, -nx};
					}
				};
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
				eq({wx(0,1),y0,wz(0,1)},{wx(0,0),y0,wz(0,0)},{wx(1,0),y0,wz(1,0)},{wx(1,1),y0,wz(1,1)}, {0,-1,0},        cs);
				eq({wx(0,0),y0,wz(0,0)},{wx(0,0),y5,wz(0,0)},{wx(1,0),y5,wz(1,0)},{wx(1,0),y0,wz(1,0)}, rn(0,-1),         cs);
				eq({wx(0,0),y5,wz(0,0)},{wx(0,.5f),y5,wz(0,.5f)},{wx(1,.5f),y5,wz(1,.5f)},{wx(1,0),y5,wz(1,0)}, {0,1,0}, ct);
				eq({wx(0,.5f),y5,wz(0,.5f)},{wx(0,.5f),y1,wz(0,.5f)},{wx(1,.5f),y1,wz(1,.5f)},{wx(1,.5f),y5,wz(1,.5f)}, rn(0,-1), cs);
				eq({wx(0,.5f),y1,wz(0,.5f)},{wx(0,1),y1,wz(0,1)},{wx(1,1),y1,wz(1,1)},{wx(1,.5f),y1,wz(1,.5f)}, {0,1,0}, ct);
				eq({wx(1,1),y0,wz(1,1)},{wx(1,1),y1,wz(1,1)},{wx(0,1),y1,wz(0,1)},{wx(0,1),y0,wz(0,1)}, rn(0,1),           cs);
				eq({wx(0,1),y0,wz(0,1)},{wx(0,1),y5,wz(0,1)},{wx(0,0),y5,wz(0,0)},{wx(0,0),y0,wz(0,0)}, rn(-1,0),          cs);
				eq({wx(0,1),y5,wz(0,1)},{wx(0,1),y1,wz(0,1)},{wx(0,.5f),y1,wz(0,.5f)},{wx(0,.5f),y5,wz(0,.5f)}, rn(-1,0),  cs);
				eq({wx(1,0),y0,wz(1,0)},{wx(1,0),y5,wz(1,0)},{wx(1,1),y5,wz(1,1)},{wx(1,1),y0,wz(1,1)}, rn(1,0),           cs);
				eq({wx(1,.5f),y5,wz(1,.5f)},{wx(1,.5f),y1,wz(1,.5f)},{wx(1,1),y1,wz(1,1)},{wx(1,1),y5,wz(1,1)}, rn(1,0),   cs);
			} else if (bdef.mesh_type == MeshType::Slab) {
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)];
				bool top = (p2 & 0x1) != 0;
				float y0 = fy + (top ? 0.5f : 0.0f);
				float y1 = fy + (top ? 1.0f : 0.5f);
				emitBox(fx, y0, fz, fx+1, y1, fz+1,
				        bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::Pillar) {
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)];
				const float t = 0.25f;
				switch (p2 % 3) {
					case 0:  // Y-axis
						emitBox(fx+t, fy,     fz+t, fx+1-t, fy+1,   fz+1-t,
						        bdef.color_top, bdef.color_side, a); break;
					case 1:  // X-axis
						emitBox(fx,   fy+t,   fz+t, fx+1,   fy+1-t, fz+1-t,
						        bdef.color_top, bdef.color_side, a); break;
					case 2:  // Z-axis
						emitBox(fx+t, fy+t,   fz,   fx+1-t, fy+1-t, fz+1,
						        bdef.color_top, bdef.color_side, a); break;
				}
			} else if (bdef.mesh_type == MeshType::Trapdoor) {
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)];
				bool open = (p2 & 0x4) != 0;
				if (!open) {
					emitBox(fx, fy, fz, fx+1, fy+0.1f, fz+1,
					        bdef.color_top, bdef.color_side, a);
				} else switch (p2 & 0x3) {
					case 0: emitBox(fx, fy, fz,      fx+1,    fy+1, fz+0.1f,
					                bdef.color_top, bdef.color_side, a); break;
					case 1: emitBox(fx+0.9f, fy, fz, fx+1,    fy+1, fz+1,
					                bdef.color_top, bdef.color_side, a); break;
					case 2: emitBox(fx, fy, fz+0.9f, fx+1,    fy+1, fz+1,
					                bdef.color_top, bdef.color_side, a); break;
					case 3: emitBox(fx, fy, fz,      fx+0.1f, fy+1, fz+1,
					                bdef.color_top, bdef.color_side, a); break;
				}
			} else if (bdef.mesh_type == MeshType::Torch) {
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)];
				const float r = 0.07f;
				const float len = 0.6f;
				float x0,y0,z0,x1,y1,z1;
				switch (p2 % 6) {
					case 0:  x0=fx+0.5f-r; y0=fy;            z0=fz+0.5f-r;
					         x1=fx+0.5f+r; y1=fy+len;        z1=fz+0.5f+r; break;
					case 1:  x0=fx+0.65f;  y0=fy+0.2f;       z0=fz+0.5f-r;
					         x1=fx+0.85f;  y1=fy+0.2f+len;   z1=fz+0.5f+r; break;
					case 2:  x0=fx+0.15f;  y0=fy+0.2f;       z0=fz+0.5f-r;
					         x1=fx+0.35f;  y1=fy+0.2f+len;   z1=fz+0.5f+r; break;
					case 3:  x0=fx+0.5f-r; y0=fy+0.2f;       z0=fz+0.65f;
					         x1=fx+0.5f+r; y1=fy+0.2f+len;   z1=fz+0.85f; break;
					case 4:  x0=fx+0.5f-r; y0=fy+0.2f;       z0=fz+0.15f;
					         x1=fx+0.5f+r; y1=fy+0.2f+len;   z1=fz+0.35f; break;
					default: x0=fx+0.5f-r; y0=fy+1.0f-len;   z0=fz+0.5f-r;
					         x1=fx+0.5f+r; y1=fy+1.0f;       z1=fz+0.5f+r; break;
				}
				emitBox(x0, y0, z0, x1, y1, z1,
				        bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::CornerStair) {
				// Bottom slab + two upper quarters forming an L whose
				// missing corner points at p2's direction.
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)] & 0x3;
				emitBox(fx, fy, fz, fx+1, fy+0.5f, fz+1,
				        bdef.color_top, bdef.color_side, a);
				float ax0,az0,ax1,az1, bx0,bz0,bx1,bz1;
				switch (p2) {
					case 0: ax0=0;    az0=0;    ax1=0.5f; az1=1;
					        bx0=0.5f; bz0=0;    bx1=1;    bz1=0.5f; break;
					case 1: ax0=0.5f; az0=0;    ax1=1;    az1=1;
					        bx0=0;    bz0=0;    bx1=0.5f; bz1=0.5f; break;
					case 2: ax0=0.5f; az0=0;    ax1=1;    az1=1;
					        bx0=0;    bz0=0.5f; bx1=0.5f; bz1=1;    break;
					default:ax0=0;    az0=0;    ax1=0.5f; az1=1;
					        bx0=0.5f; bz0=0.5f; bx1=1;    bz1=1;    break;
				}
				emitBox(fx+ax0, fy+0.5f, fz+az0, fx+ax1, fy+1, fz+az1,
				        bdef.color_top, bdef.color_side, a);
				emitBox(fx+bx0, fy+0.5f, fz+bz0, fx+bx1, fy+1, fz+bz1,
				        bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::Door) {
				emitBox(fx, fy, fz, fx+1, fy+1, fz+0.1f,
				        bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::DoorOpen) {
				uint8_t p2 = padded.param2[padIdx(lx, ly, lz)];
				bool hingeRight = (p2 >> 2) & 1;
				if (hingeRight)
					emitBox(fx+0.9f, fy, fz, fx+1, fy+1, fz+1, bdef.color_top, bdef.color_side, a);
				else
					emitBox(fx, fy, fz, fx+0.1f, fy+1, fz+1, bdef.color_top, bdef.color_side, a);
			} else if (bdef.mesh_type == MeshType::Plant) {
				// Plant decoration — Bezier blades keyed off BlockDef colors and
				// the per-cell appearance tint. Height and blade count both scale
				// with the palette index so a cluster reads as a visual mound:
				// short fringe at the edge (idx 1) → tall dramatic tuft at the
				// core (idx 5). The exact tier meanings are defined by the
				// content block that uses the Plant mesh (see world_template.h
				// tallGrassRoll for tall_grass).
				glm::vec3 rootCol = bdef.color_side;
				glm::vec3 tipCol  = bdef.color_top;
				float heightScale = 0.85f;
				int bladeCount    = 4;
				if (!bdef.appearance_palette.empty()) {
					uint8_t appIdx = padded.appearance[padIdx(lx, ly, lz)];
					if (appIdx >= bdef.appearance_palette.size()) appIdx = 0;
					glm::vec3 tint = bdef.appearance_palette[appIdx].tint;
					rootCol *= tint;
					tipCol  *= tint;
					// Height table — wide range so the center vs edge of a
					// cluster reads as obviously different heights. Index 5
					// exceeds 1 block so the core tufts poke above eye-level.
					static constexpr float HEIGHT_BY_TIER[6] = {
						0.85f,  // 0 default (unused for tall_grass)
						0.55f,  // 1 fringe — short
						0.80f,  // 2 outer
						1.10f,  // 3 inner
						1.45f,  // 4 near-core
						1.90f,  // 5 core — very tall (> 1 block)
					};
					heightScale = HEIGHT_BY_TIER[appIdx];
					// Taller tufts also have more blades so the core reads as
					// a dense tussock, not one stretched blade.
					bladeCount = 3 + (int)appIdx;  // 3..8
				}
				emitPlant(wx, wy, wz, rootCol, tipCol, heightScale, bladeCount);
			}
			continue;
		}

		for (int face = 0; face < 6; face++) {
			int nlx = lx + FACE_DIRS[face].x;
			int nly = ly + FACE_DIRS[face].y;
			int nlz = lz + FACE_DIRS[face].z;

			BlockId neighbor = cachedBlock(nlx, nly, nlz);
			const BlockDef& ndef = reg.get(neighbor);
			bool isYFace = (face == 2 || face == 3);
			if (ndef.solid && !ndef.transparent && (isYFace || ndef.mesh_type == MeshType::Cube)) continue;

			glm::vec3 color;
			if (face == 2)      color = bdef.color_top;
			else if (face == 3) color = bdef.color_bottom;
			else                color = bdef.color_side;

			// Appearance tint multiplies base face color (I5). Empty palette
			// or index 0 are pass-through; out-of-range is clamped to default.
			if (!bdef.appearance_palette.empty()) {
				uint8_t appIdx = padded.appearance[padIdx(lx, ly, lz)];
				if (appIdx >= bdef.appearance_palette.size()) appIdx = 0;
				color *= bdef.appearance_palette[appIdx].tint;
			}

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

} // namespace solarium

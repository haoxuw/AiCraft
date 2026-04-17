#include "client/renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace civcraft {

static const float PI = 3.14159265f;

// --- Frustum culling ---
struct Frustum {
	glm::vec4 planes[6]; // left, right, bottom, top, near, far

	void extract(const glm::mat4& vp) {
		// Extract from columns of VP matrix
		for (int i = 0; i < 3; i++) {
			planes[i*2+0] = glm::vec4(
				vp[0][3] + vp[0][i],
				vp[1][3] + vp[1][i],
				vp[2][3] + vp[2][i],
				vp[3][3] + vp[3][i]);
			planes[i*2+1] = glm::vec4(
				vp[0][3] - vp[0][i],
				vp[1][3] - vp[1][i],
				vp[2][3] - vp[2][i],
				vp[3][3] - vp[3][i]);
		}
		// Normalize
		for (auto& p : planes) {
			float len = glm::length(glm::vec3(p));
			if (len > 0.0f) p /= len;
		}
	}

	bool testAABB(glm::vec3 mn, glm::vec3 mx) const {
		for (auto& p : planes) {
			glm::vec3 pos(
				p.x > 0 ? mx.x : mn.x,
				p.y > 0 ? mx.y : mn.y,
				p.z > 0 ? mx.z : mn.z);
			if (glm::dot(glm::vec3(p), pos) + p.w < 0.0f)
				return false;
		}
		return true;
	}
};

void Renderer::setTimeOfDay(float t) {
	m_timeOfDay = t - std::floor(t); // wrap to 0..1

	// Sun angle: 0.25 = sunrise (east), 0.5 = noon (overhead), 0.75 = sunset (west)
	float angle = (m_timeOfDay - 0.25f) * 2.0f * PI;
	float sunHeight = std::sin(angle);

	m_sunDir = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.3f));

	// Sun strength: 1.0 when high, fading to 0.0 at night
	m_sunStrength = std::clamp(sunHeight * 3.0f + 0.3f, 0.0f, 1.0f);

	// Sky colors based on sun height
	if (sunHeight > 0.15f) {
		// Day
		float df = std::clamp((sunHeight - 0.15f) / 0.35f, 0.0f, 1.0f);
		glm::vec3 daySky = {0.40f, 0.60f, 0.85f};
		glm::vec3 dawnSky = {0.50f, 0.45f, 0.65f};
		m_skyColor = glm::mix(dawnSky, daySky, df);

		glm::vec3 dayHorizon = {0.70f, 0.82f, 0.92f};
		glm::vec3 dawnHorizon = {0.85f, 0.60f, 0.45f};
		m_horizonColor = glm::mix(dawnHorizon, dayHorizon, df);
	} else if (sunHeight > -0.15f) {
		// Sunrise/sunset transition
		float sf = (sunHeight + 0.15f) / 0.30f; // 0..1
		glm::vec3 sunsetSky = {0.25f, 0.15f, 0.35f};
		glm::vec3 dawnSky = {0.50f, 0.45f, 0.65f};
		m_skyColor = glm::mix(sunsetSky, dawnSky, sf);

		glm::vec3 sunsetHorizon = {0.90f, 0.45f, 0.25f};
		glm::vec3 dawnHorizon = {0.85f, 0.60f, 0.45f};
		m_horizonColor = glm::mix(sunsetHorizon, dawnHorizon, sf);
	} else {
		// Night
		float nf = std::clamp((-sunHeight - 0.15f) / 0.35f, 0.0f, 1.0f);
		glm::vec3 duskSky = {0.25f, 0.15f, 0.35f};
		glm::vec3 nightSky = {0.03f, 0.03f, 0.08f};
		m_skyColor = glm::mix(duskSky, nightSky, nf);

		glm::vec3 duskHorizon = {0.30f, 0.15f, 0.20f};
		glm::vec3 nightHorizon = {0.05f, 0.05f, 0.10f};
		m_horizonColor = glm::mix(duskHorizon, nightHorizon, nf);
	}
}

bool Renderer::init(rhi::IRhi* rhi, const std::string& dir) {
	m_rhi = rhi;
	// Terrain + sky shaders are owned by the RHI now (terrain.vert/frag and
	// sky.vert/frag live under platform/shaders/, loaded lazily by the GL
	// backend on first use).
	if (!m_crosshairShader.loadFromFile(dir + "/crosshair.vert", dir + "/crosshair.frag"))
		return false;
	if (!m_highlightShader.loadFromFile(dir + "/highlight.vert", dir + "/highlight.frag"))
		return false;

	// Crosshair — 4 lines with gap in center + center dot
	// Gap = inner 0.006, outer = 0.024 (each arm)
	float g = 0.006f, o = 0.024f;
	float ch[] = {
		// Horizontal arms (left, right)
		-o, 0,  -g, 0,
		 g, 0,   o, 0,
		// Vertical arms (bottom, top)
		0, -o*1.5f,  0, -g*1.5f,
		0,  g*1.5f,  0,  o*1.5f,
		// Center dot (small quad as 2 triangles — 6 verts)
		-0.003f,-0.003f,  0.003f,-0.003f,  0.003f, 0.003f,
		-0.003f,-0.003f,  0.003f, 0.003f, -0.003f, 0.003f,
	};
	glGenVertexArrays(1, &m_crosshairVAO);
	glGenBuffers(1, &m_crosshairVBO);
	glBindVertexArray(m_crosshairVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_crosshairVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(ch), ch, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	// Block highlight: 24 line vertices (wireframe) + 36 triangle vertices (filled faces for break overlay)
	float h[] = {
		// Lines: 12 edges (24 vertices)
		0,0,0, 1,0,0,  1,0,0, 1,0,1,  1,0,1, 0,0,1,  0,0,1, 0,0,0,  // bottom
		0,1,0, 1,1,0,  1,1,0, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,1,0,  // top
		0,0,0, 0,1,0,  1,0,0, 1,1,0,  1,0,1, 1,1,1,  0,0,1, 0,1,1,  // verticals
		// Faces: 6 faces × 2 triangles (36 vertices) — for break overlay
		0,0,1, 1,0,1, 1,1,1,  0,0,1, 1,1,1, 0,1,1,  // front  (z=1)
		1,0,0, 0,0,0, 0,1,0,  1,0,0, 0,1,0, 1,1,0,  // back   (z=0)
		1,0,0, 1,0,1, 1,1,1,  1,0,0, 1,1,1, 1,1,0,  // right  (x=1)
		0,0,1, 0,0,0, 0,1,0,  0,0,1, 0,1,0, 0,1,1,  // left   (x=0)
		0,1,0, 1,1,0, 1,1,1,  0,1,0, 1,1,1, 0,1,1,  // top    (y=1)
		0,0,0, 1,0,0, 1,0,1,  0,0,0, 1,0,1, 0,0,1,  // bottom (y=0)
	};
	glGenVertexArrays(1, &m_highlightVAO);
	glGenBuffers(1, &m_highlightVBO);
	glBindVertexArray(m_highlightVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_highlightVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(h), h, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	// Crack overlay (dynamic line data for block break progress)
	glGenVertexArrays(1, &m_crackVAO);
	glGenBuffers(1, &m_crackVBO);
	glBindVertexArray(m_crackVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_crackVBO);
	glBufferData(GL_ARRAY_BUFFER, 2048 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	// Plan path dynamic VAO (position-only, used with highlight shader)
	glGenVertexArrays(1, &m_pathVAO);
	glGenBuffers(1, &m_pathVBO);
	glBindVertexArray(m_pathVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_pathVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * 512, nullptr, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	// Door-anim mesh handle is allocated lazily on first renderDoorAnims call
	// (createChunkMesh + updateChunkMesh thereafter — one allocation, then
	// per-frame re-uploads while a door is mid-animation).

	// UI quad (2D, reusable for hotbar slots)
	float uq[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
	glGenVertexArrays(1, &m_quadVAO);
	glGenBuffers(1, &m_quadVBO);
	glBindVertexArray(m_quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(uq), uq, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);

	m_modelRenderer.init(&m_highlightShader);
	m_fogOfWar.init(dir);
	return true;
}

void Renderer::shutdown() {
	for (auto& [_, slot] : m_meshes) {
		if (slot.opaque)      m_rhi->destroyMesh(slot.opaque);
		if (slot.transparent) m_rhi->destroyMesh(slot.transparent);
	}
	m_meshes.clear();
	auto del = [](GLuint& vao, GLuint& vbo) {
		if (vbo) glDeleteBuffers(1, &vbo);
		if (vao) glDeleteVertexArrays(1, &vao);
		vao = vbo = 0;
	};
	del(m_crosshairVAO, m_crosshairVBO);
	del(m_highlightVAO, m_highlightVBO);
	del(m_crackVAO, m_crackVBO);
	del(m_quadVAO, m_quadVBO);
	if (m_doorAnimMesh) {
		m_rhi->destroyMesh(m_doorAnimMesh);
		m_doorAnimMesh = rhi::IRhi::kInvalidMesh;
	}
	m_modelRenderer.shutdown();
	m_fogOfWar.shutdown();
}

void Renderer::markChunkDirty(ChunkPos pos) {
	// Destroy mesh immediately so it gets rebuilt as a "new" chunk in Phase 2
	// of updateChunks (sorted closest-first, so player-adjacent chunks rebuild first).
	auto it = m_meshes.find(pos);
	if (it != m_meshes.end()) {
		if (it->second.opaque)      m_rhi->destroyMesh(it->second.opaque);
		if (it->second.transparent) m_rhi->destroyMesh(it->second.transparent);
		m_meshes.erase(it);
	}
	m_dirtyChunks.erase(pos);
}

// Build (or rebuild) the RHI handle pair for one chunk. Reuses existing
// handles via updateChunkMesh when the slot already has buffers; otherwise
// allocates fresh ones via createChunkMesh. Empty side → handle stays
// kInvalidMesh and the corresponding draw is skipped.
static void uploadChunkSlot(rhi::IRhi* rhi,
                            Renderer::ChunkMeshSlot& slot,
                            const std::vector<ChunkVertex>& opaque,
                            const std::vector<ChunkVertex>& transparent,
                            ChunkPos cp) {
	auto upload = [&](rhi::IRhi::MeshHandle& h, uint32_t& storedCount,
	                  const std::vector<ChunkVertex>& v) {
		if (v.empty()) {
			if (h) { rhi->destroyMesh(h); h = rhi::IRhi::kInvalidMesh; }
			storedCount = 0;
			return;
		}
		const float* data = reinterpret_cast<const float*>(v.data());
		const uint32_t n = (uint32_t)v.size();
		if (h) rhi->updateChunkMesh(h, data, n);
		else   h = rhi->createChunkMesh(data, n);
		storedCount = n;
	};
	upload(slot.opaque,      slot.opaqueVerts,      opaque);
	upload(slot.transparent, slot.transparentVerts, transparent);
	slot.mn = glm::vec3(cp.x * CHUNK_SIZE, cp.y * CHUNK_SIZE, cp.z * CHUNK_SIZE);
	slot.mx = slot.mn + glm::vec3(CHUNK_SIZE);
}

void Renderer::meshAllPending(ChunkSource& world, const Camera& cam, int renderDistance) {
	// Called once at world load — no throttle, mesh everything
	ChunkPos center = worldToChunk(
		(int)std::floor(cam.position.x),
		(int)std::floor(cam.position.y),
		(int)std::floor(cam.position.z));

	for (int cy = center.y - 3; cy <= center.y + 3; cy++)
		for (int cz = center.z - renderDistance; cz <= center.z + renderDistance; cz++)
			for (int cx = center.x - renderDistance; cx <= center.x + renderDistance; cx++) {
				ChunkPos cp = {cx, cy, cz};
				if (m_meshes.count(cp)) continue;
				Chunk* chunk = world.getChunk(cp);
				if (!chunk) continue;
				auto [oVerts, tVerts] = m_mesher.buildMesh(world, cp);
				ChunkMeshSlot slot;
				uploadChunkSlot(m_rhi, slot, oVerts, tVerts, cp);
				m_meshes[cp] = slot;
			}
}

void Renderer::updateChunks(ChunkSource& world, const Camera& cam, int renderDistance) {
	ChunkPos center = worldToChunk(
		(int)std::floor(cam.position.x),
		(int)std::floor(cam.position.y),
		(int)std::floor(cam.position.z));

	// --- Phase 1: Find unmeshed chunks, sorted closest-first ---
	struct PendingChunk { ChunkPos pos; int distSq; };
	std::vector<PendingChunk> pending;
	pending.reserve(64); // typically only edge chunks are new

	for (int cy = center.y - 3; cy <= center.y + 3; cy++)
		for (int cz = center.z - renderDistance; cz <= center.z + renderDistance; cz++)
			for (int cx = center.x - renderDistance; cx <= center.x + renderDistance; cx++) {
				ChunkPos cp = {cx, cy, cz};
				if (m_meshes.count(cp)) continue;
				int dx = cx - center.x, dy = cy - center.y, dz = cz - center.z;
				pending.push_back({cp, dx*dx + dy*dy + dz*dz});
			}

	std::sort(pending.begin(), pending.end(),
		[](const PendingChunk& a, const PendingChunk& b) { return a.distSq < b.distSq; });

	// --- Phase 2: Generate + mesh new chunks (throttled) ---
	// Mark neighbors as needing re-mesh (deferred, not immediate)
	constexpr int MAX_NEW_PER_FRAME = 16;
	int newBuilt = 0;

	for (auto& pc : pending) {
		if (newBuilt >= MAX_NEW_PER_FRAME) break;

		Chunk* chunk = world.getChunk(pc.pos);
		if (!chunk) continue;

		auto [oVerts, tVerts] = m_mesher.buildMesh(world, pc.pos);
		ChunkMeshSlot slot;
		uploadChunkSlot(m_rhi, slot, oVerts, tVerts, pc.pos);
		m_meshes[pc.pos] = slot;
		newBuilt++;

		// Mark 6 face-adjacent neighbors as dirty (deferred re-mesh)
		for (auto& dir : FACE_DIRS) {
			ChunkPos np = {pc.pos.x + dir.x, pc.pos.y + dir.y, pc.pos.z + dir.z};
			if (m_meshes.count(np))
				m_dirtyChunks.insert(np);
		}
	}

	// --- Phase 3: Re-mesh dirty neighbors (throttled separately) ---
	constexpr int MAX_REMESH_PER_FRAME = 8;
	int remeshed = 0;

	auto it = m_dirtyChunks.begin();
	while (it != m_dirtyChunks.end() && remeshed < MAX_REMESH_PER_FRAME) {
		ChunkPos dp = *it;
		it = m_dirtyChunks.erase(it);

		auto mit = m_meshes.find(dp);
		if (mit == m_meshes.end()) continue;

		auto [oVerts, tVerts] = m_mesher.buildMesh(world, dp);
		// Reuse existing handles via update — keeps the slot live for any
		// in-flight rendering (matters on the VK backend; harmless on GL).
		uploadChunkSlot(m_rhi, mit->second, oVerts, tVerts, dp);
		remeshed++;
	}

	// --- Phase 4: Unload distant meshes + world chunks ---
	std::vector<ChunkPos> toRemove;
	int unloadDistSq = (renderDistance + 2) * (renderDistance + 2);
	for (auto& [pos, slot] : m_meshes) {
		int dx = pos.x - center.x, dz = pos.z - center.z;
		if (dx*dx + dz*dz > unloadDistSq) {
			if (slot.opaque)      m_rhi->destroyMesh(slot.opaque);
			if (slot.transparent) m_rhi->destroyMesh(slot.transparent);
			toRemove.push_back(pos);
		}
	}
	for (auto& p : toRemove) {
		m_meshes.erase(p);
		m_dirtyChunks.erase(p);
	}

	// Unload world chunk data too (free memory)
	world.unloadDistantChunks(center, renderDistance);
}

void Renderer::render(const Camera& cam, float aspect, glm::ivec3* highlight,
                      int selectedSlot, int hotbarSize,
                      glm::vec2 crosshairOffset, bool showCrosshair) {
	glClearColor(m_skyColor.r, m_skyColor.g, m_skyColor.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	renderSky(cam, aspect);
	renderTerrain(cam, aspect);
	if (highlight) renderHighlight(cam, aspect, *highlight);
	if (showCrosshair) renderCrosshair(aspect, crosshairOffset);
}

void Renderer::renderFogOfWar(const Camera& cam, float aspect,
                              ChunkSource& chunks, int renderDistance) {
	m_fogOfWar.render(cam, aspect, chunks, renderDistance, m_horizonColor, m_timeOfDay);
}

void Renderer::renderSky(const Camera& cam, float aspect) {
	glm::mat4 invVP = glm::inverse(cam.projectionMatrix(aspect) * cam.viewMatrix());
	m_rhi->drawSky(&invVP[0][0],
	               &m_skyColor.x,
	               &m_horizonColor.x,
	               &m_sunDir.x,
	               m_sunStrength,
	               m_time);
}

void Renderer::renderTerrain(const Camera& cam, float aspect) {
	// Build the SceneParams snapshot once and feed every chunk draw through
	// the RHI. State setup (depth/cull/blend) lives in the backend's per-pass
	// drawChunkMesh* implementation, not here.
	glm::mat4 view = cam.viewMatrix();
	glm::mat4 proj = cam.projectionMatrix(aspect);
	glm::mat4 vp   = proj * view;

	rhi::IRhi::SceneParams scene{};
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float) * 16);
	scene.camPos[0] = cam.position.x;
	scene.camPos[1] = cam.position.y;
	scene.camPos[2] = cam.position.z;
	scene.time      = m_time;
	scene.sunDir[0] = m_sunDir.x;
	scene.sunDir[1] = m_sunDir.y;
	scene.sunDir[2] = m_sunDir.z;
	scene.sunStr    = m_sunStrength;

	const float fogColor[3] = { m_horizonColor.r, m_horizonColor.g, m_horizonColor.b };

	Frustum frustum;
	frustum.extract(vp);

	// ── Pass 1: opaque ── unsorted, just iterate in map order.
	for (auto& [pos, slot] : m_meshes) {
		if (!slot.opaque) continue;
		if (!frustum.testAABB(slot.mn, slot.mx)) continue;
		m_rhi->drawChunkMeshOpaque(scene, fogColor, m_fogStart, m_fogEnd, slot.opaque);
	}

	// ── Pass 2: transparent ── sort back-to-front for correct compositing.
	struct TChunk { float distSq; ChunkPos pos; };
	std::vector<TChunk> tChunks;
	for (auto& [pos, slot] : m_meshes) {
		if (!slot.transparent) continue;
		if (!frustum.testAABB(slot.mn, slot.mx)) continue;
		glm::vec3 center = (slot.mn + slot.mx) * 0.5f;
		glm::vec3 d = center - cam.position;
		tChunks.push_back({ glm::dot(d, d), pos });
	}
	if (!tChunks.empty()) {
		std::sort(tChunks.begin(), tChunks.end(),
			[](const TChunk& a, const TChunk& b) { return a.distSq > b.distSq; });
		for (auto& tc : tChunks) {
			auto& slot = m_meshes[tc.pos];
			m_rhi->drawChunkMeshTransparent(scene, fogColor,
				m_fogStart, m_fogEnd, slot.transparent);
		}
	}
}

void Renderer::renderHighlight(const Camera& cam, float aspect, glm::ivec3 pos) {
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Slightly inset to avoid z-fighting (-0.002 on each side)
	glm::mat4 model = glm::translate(glm::mat4(1.0f),
		glm::vec3(pos) + glm::vec3(-0.002f));
	model = glm::scale(model, glm::vec3(1.004f));

	glm::mat4 mvp = cam.projectionMatrix(aspect) * cam.viewMatrix() * model;

	m_highlightShader.use();
	m_highlightShader.setMat4("uMVP", mvp);
	GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");

	glBindVertexArray(m_highlightVAO);

	// Black outline pass (slightly thicker)
	glUniform4f(loc, 0.0f, 0.0f, 0.0f, 0.55f);
	glLineWidth(3.5f);
	glDrawArrays(GL_LINES, 0, 24);

	// White inner pass
	glUniform4f(loc, 1.0f, 1.0f, 1.0f, 0.65f);
	glLineWidth(1.5f);
	glDrawArrays(GL_LINES, 0, 24);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
}

void Renderer::renderMoveTarget(const Camera& cam, float aspect, glm::ivec3 pos) {
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Slightly expanded, green tint
	glm::mat4 model = glm::translate(glm::mat4(1.0f),
		glm::vec3(pos) + glm::vec3(-0.01f));
	model = glm::scale(model, glm::vec3(1.02f));

	glm::mat4 mvp = cam.projectionMatrix(aspect) * cam.viewMatrix() * model;

	m_highlightShader.use();
	m_highlightShader.setMat4("uMVP", mvp);
	m_highlightShader.setMat4("uModel", model);
	m_highlightShader.setVec3("uSunDir", m_sunDir);

	GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");
	glUniform4f(loc, 0.1f, 0.8f, 0.2f, 0.6f); // green

	glBindVertexArray(m_highlightVAO);
	glLineWidth(3.0f);
	glDrawArrays(GL_LINES, 0, 24);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
}

void Renderer::renderCrosshair(float aspect, glm::vec2 center) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_crosshairShader.use();
	m_crosshairShader.setFloat("uAspect", aspect);
	m_crosshairShader.setVec2("uCenter", center);

	glBindVertexArray(m_crosshairVAO);

	// Hitmarker: flash crosshair color when attack connects
	glm::vec3 flashColor = {1, 1, 1};
	float flashBlend = 0.0f;
	if (m_hitmarkerTimer > 0.0f) {
		flashBlend = m_hitmarkerTimer / 0.18f; // 1→0 over 0.18s
		flashColor = m_hitmarkerKill ? glm::vec3(1.0f, 0.15f, 0.05f) // red: kill shot
		                             : glm::vec3(1.0f, 0.80f, 0.25f); // orange: damage
	}
	glm::vec4 lineColor = {
		glm::mix(1.0f, flashColor.r, flashBlend),
		glm::mix(1.0f, flashColor.g, flashBlend),
		glm::mix(1.0f, flashColor.b, flashBlend),
		0.9f + flashBlend * 0.1f
	};

	// Draw the 4 line arms (8 verts = 4 lines)
	// White with black outline effect: draw black slightly thicker first, then white/flash
	m_crosshairShader.setVec4("uColor", {0, 0, 0, 0.6f});
	glLineWidth(3.0f);
	glDrawArrays(GL_LINES, 0, 8);

	m_crosshairShader.setVec4("uColor", lineColor);
	glLineWidth(1.5f);
	glDrawArrays(GL_LINES, 0, 8);

	// Draw center dot (6 verts = 2 triangles, starting at vertex 8)
	m_crosshairShader.setVec4("uColor", {lineColor.r, lineColor.g, lineColor.b, 0.85f});
	glDrawArrays(GL_TRIANGLES, 8, 6);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void Renderer::renderBreakProgress(const Camera& cam, float aspect, glm::ivec3 pos, float progress) {
	// Crack line segments (u,v pairs on each face, [0,1] range).
	// Each stage adds more cracks for a progressively shattered look.
	static const float cracks[][4] = {
		// Stage 1: initial fracture from center (0–4)
		{0.50f,0.52f, 0.18f,0.85f}, {0.50f,0.52f, 0.80f,0.22f},
		{0.50f,0.52f, 0.82f,0.70f}, {0.28f,0.73f, 0.08f,0.92f},
		{0.72f,0.38f, 0.90f,0.15f},
		// Stage 2: branches spread (5–10)
		{0.50f,0.52f, 0.25f,0.15f}, {0.25f,0.15f, 0.05f,0.28f},
		{0.82f,0.70f, 1.00f,0.82f}, {0.18f,0.85f, 0.00f,0.65f},
		{0.50f,0.52f, 0.60f,0.90f}, {0.60f,0.90f, 0.45f,1.00f},
		// Stage 3: dense fracture network (11–18)
		{0.80f,0.22f, 0.95f,0.00f}, {0.05f,0.28f, 0.00f,0.05f},
		{0.25f,0.15f, 0.40f,0.00f}, {0.82f,0.70f, 0.72f,0.95f},
		{0.18f,0.85f, 0.30f,1.00f}, {0.90f,0.15f, 1.00f,0.00f},
		{0.60f,0.90f, 0.80f,1.00f}, {0.08f,0.92f, 0.00f,1.00f},
	};
	static const int stageEnd[] = {5, 11, 19}; // segment count per stage (cumulative)

	int stage = (progress < 0.34f) ? 0 : (progress < 0.67f) ? 1 : 2;
	int numSegs = stageEnd[stage];

	// Map 2D face coords (u,v) to 3D for each of the 6 cube faces.
	// Slight offset (±0.001) to avoid z-fighting with the block surface.
	auto facePoint = [](int face, float u, float v) -> glm::vec3 {
		switch (face) {
		case 0: return {u,     v,     1.001f};     // front  z=1
		case 1: return {1-u,   v,    -0.001f};     // back   z=0
		case 2: return {1.001f, v,    1-u};         // right  x=1
		case 3: return {-0.001f,v,    u};           // left   x=0
		case 4: return {u,     1.001f, v};          // top    y=1
		case 5: return {u,    -0.001f, 1-v};        // bottom y=0
		default: return {u, v, 0};
		}
	};

	// Generate crack line vertices for all 6 faces
	std::vector<float> verts;
	verts.reserve(numSegs * 6 * 2 * 3); // segs * faces * 2 endpoints * 3 floats
	for (int face = 0; face < 6; face++) {
		for (int s = 0; s < numSegs; s++) {
			glm::vec3 a = facePoint(face, cracks[s][0], cracks[s][1]);
			glm::vec3 b = facePoint(face, cracks[s][2], cracks[s][3]);
			verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
			verts.push_back(b.x); verts.push_back(b.y); verts.push_back(b.z);
		}
	}

	glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos));
	glm::mat4 mvp = cam.projectionMatrix(aspect) * cam.viewMatrix() * model;

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_highlightShader.use();
	m_highlightShader.setMat4("uMVP", mvp);
	GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");

	// Upload crack line data
	glBindVertexArray(m_crackVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_crackVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
	int lineCount = (int)(verts.size() / 3);

	// Draw: black outline pass then dark crack lines
	glUniform4f(loc, 0.0f, 0.0f, 0.0f, 0.35f + progress * 0.35f);
	glLineWidth(3.5f);
	glDrawArrays(GL_LINES, 0, lineCount);

	glUniform4f(loc, 0.08f, 0.06f, 0.04f, 0.55f + progress * 0.35f);
	glLineWidth(1.8f);
	glDrawArrays(GL_LINES, 0, lineCount);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
}

// Old renderPlayerModel removed -- use ModelRenderer::draw() instead

void Renderer::renderDoorAnims(const Camera& cam, float aspect,
                               const std::vector<DoorAnim>& anims) {
	if (anims.empty()) return;

	static const float kDuration = 0.25f;

	// Build vertices for all animating doors
	std::vector<ChunkVertex> verts;
	verts.reserve(anims.size() * 12);

	for (auto& a : anims) {
		float t = std::min(a.timer / kDuration, 1.0f);
		// Ease in-out: smoothstep
		t = t * t * (3.0f - 2.0f * t);

		// Angle: opening goes 0→PI/2, closing goes PI/2→0
		float theta = a.opening ? (t * (float)M_PI * 0.5f)
		                        : ((1.0f - t) * (float)M_PI * 0.5f);

		float fx = (float)a.basePos.x;
		float fy = (float)a.basePos.y;
		float fz = (float)a.basePos.z;
		float h  = (float)a.height;

		// Hinge pivot and free-edge endpoints
		float pivX, pivZ, farX0, farZ0;
		if (!a.hingeRight) {
			// Left hinge: pivot at (fx, fz)
			pivX = fx;     pivZ = fz;
			// Free edge at (fx+1, fz) rotates CCW → (fx+cos θ, fz+sin θ)
			farX0 = fx + std::cos(theta);
			farZ0 = fz + std::sin(theta);
		} else {
			// Right hinge: pivot at (fx+1, fz)
			pivX = fx + 1.f; pivZ = fz;
			// Free edge at (fx, fz) rotates CW → (fx+1-cos θ, fz+sin θ)
			farX0 = fx + 1.f - std::cos(theta);
			farZ0 = fz + std::sin(theta);
		}

		// Panel normal (perpendicular to the panel surface, rotates with door)
		float nx = -(farZ0 - pivZ);  // rotate 90° in XZ
		float nz =  (farX0 - pivX);
		float nlen = std::sqrt(nx*nx + nz*nz);
		if (nlen > 0.001f) { nx /= nlen; nz /= nlen; }
		glm::vec3 norm{nx, 0.f, nz};

		float shade = 0.85f; // side shade
		glm::vec3 col = a.color;

		// 4 corners: pivot-bottom, pivot-top, far-top, far-bottom
		ChunkVertex v0{.position={pivX,  fy,   pivZ},  .color=col, .normal=norm, .ao=1.f, .shade=shade, .alpha=1.f, .glow=0.f};
		ChunkVertex v1{.position={pivX,  fy+h, pivZ},  .color=col, .normal=norm, .ao=1.f, .shade=shade, .alpha=1.f, .glow=0.f};
		ChunkVertex v2{.position={farX0, fy+h, farZ0}, .color=col, .normal=norm, .ao=1.f, .shade=shade, .alpha=1.f, .glow=0.f};
		ChunkVertex v3{.position={farX0, fy,   farZ0}, .color=col, .normal=norm, .ao=1.f, .shade=shade, .alpha=1.f, .glow=0.f};

		// Front face (CCW)
		verts.insert(verts.end(), {v0, v1, v2, v0, v2, v3});
		// Back face (reversed winding, flipped normal)
		ChunkVertex bv0=v0, bv1=v1, bv2=v2, bv3=v3;
		glm::vec3 bn = -norm;
		bv0.normal=bv1.normal=bv2.normal=bv3.normal=bn;
		verts.insert(verts.end(), {bv0, bv3, bv2, bv0, bv2, bv1});
	}

	if (verts.empty()) return;

	// Door faces ride on the same RHI chunk-mesh pipeline as the terrain.
	// They re-mesh every frame an animation is active, so we keep one
	// persistent handle and call updateChunkMesh in place — the GL backend
	// just glBufferData's, the VK backend reuses or grows the device buffer.
	const float* data = reinterpret_cast<const float*>(verts.data());
	const uint32_t n  = (uint32_t)verts.size();
	if (!m_doorAnimMesh) m_doorAnimMesh = m_rhi->createChunkMesh(data, n);
	else                 m_rhi->updateChunkMesh(m_doorAnimMesh, data, n);

	glm::mat4 vp = cam.projectionMatrix(aspect) * cam.viewMatrix();
	rhi::IRhi::SceneParams scene{};
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float) * 16);
	scene.camPos[0] = cam.position.x;
	scene.camPos[1] = cam.position.y;
	scene.camPos[2] = cam.position.z;
	scene.time      = m_time;
	scene.sunDir[0] = m_sunDir.x;
	scene.sunDir[1] = m_sunDir.y;
	scene.sunDir[2] = m_sunDir.z;
	scene.sunStr    = m_sunStrength;
	const float fogColor[3] = { m_horizonColor.r, m_horizonColor.g, m_horizonColor.b };
	m_rhi->drawChunkMeshOpaque(scene, fogColor, m_fogStart, m_fogEnd, m_doorAnimMesh);
}

void Renderer::renderPlanPath(const Camera& cam, float aspect,
                              const std::vector<glm::vec3>& points,
                              glm::vec4 color, float dashLen, float time) {
	if (points.size() < 2) return;

	// Build dashed line segments along the polyline.
	// The "flow" effect: offset the dash pattern by time so dashes move toward destination.
	std::vector<float> verts;
	float accum = -time * 4.0f; // animated offset (flows toward destination)

	for (size_t i = 0; i + 1 < points.size(); i++) {
		glm::vec3 a = points[i];
		glm::vec3 b = points[i + 1];
		float segLen = glm::length(b - a);
		if (segLen < 0.01f) continue;
		glm::vec3 dir = (b - a) / segLen;

		// Walk along segment, emitting dash-on / dash-off
		float t = 0;
		while (t < segLen) {
			float phase = std::fmod(accum + t, dashLen * 2.0f);
			if (phase < 0) phase += dashLen * 2.0f;

			bool on = (phase < dashLen);
			float remaining = on ? (dashLen - phase) : (dashLen * 2.0f - phase);
			float end = std::min(t + remaining, segLen);

			if (on) {
				glm::vec3 p0 = a + dir * t;
				glm::vec3 p1 = a + dir * end;
				// Lift slightly to avoid z-fighting with terrain
				p0.y += 0.15f;
				p1.y += 0.15f;
				verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
				verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
			}

			t = end + 0.001f; // tiny epsilon to avoid infinite loop
		}
		accum += segLen;
	}

	if (verts.empty()) return;

	// Upload and draw
	glBindVertexArray(m_pathVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_pathVBO);
	size_t byteSize = verts.size() * sizeof(float);
	glBufferData(GL_ARRAY_BUFFER, byteSize, verts.data(), GL_DYNAMIC_DRAW);

	glm::mat4 mvp = cam.projectionMatrix(aspect) * cam.viewMatrix();

	m_highlightShader.use();
	m_highlightShader.setMat4("uMVP", mvp);
	m_highlightShader.setMat4("uModel", glm::mat4(1.0f));
	m_highlightShader.setVec3("uSunDir", glm::vec3(0, 1, 0));
	// Set default normal (up) for unbound attribute 1 — avoids normalize(0,0,0) NaN
	glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
	// Disable tinting and texture for flat color
	glUniform1f(glGetUniformLocation(m_highlightShader.id(), "uTintStrength"), 0.0f);
	glUniform1i(glGetUniformLocation(m_highlightShader.id(), "uUseTexture"), 0);
	GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");
	glUniform4f(loc, color.r, color.g, color.b, color.a);

	glDisable(GL_DEPTH_TEST);  // always visible (show through terrain)
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(3.0f);

	glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size() / 3));

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
}

void Renderer::renderMoveTargetSpinner(const Camera& cam, float aspect,
                                       glm::vec3 pos, int sides,
                                       glm::vec4 color, float spinRad) {
	if (sides < 3) sides = 3;

	// Build two concentric line-loops (outer + inner) as GL_LINES pairs,
	// spun around Y. Floats hover just above the block center.
	const float hover    = 0.9f;
	const float rOuter   = 0.55f;
	const float rInner   = 0.30f;
	std::vector<float> verts;
	verts.reserve(sides * 2 * 3 * 2);

	auto emitLoop = [&](float r, float phaseOffset) {
		for (int i = 0; i < sides; i++) {
			float a0 = spinRad + phaseOffset + (float)i     / sides * 2.0f * PI;
			float a1 = spinRad + phaseOffset + (float)(i+1) / sides * 2.0f * PI;
			glm::vec3 p0 = pos + glm::vec3(std::cos(a0) * r, hover, std::sin(a0) * r);
			glm::vec3 p1 = pos + glm::vec3(std::cos(a1) * r, hover, std::sin(a1) * r);
			verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
			verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
		}
	};
	emitLoop(rOuter, 0.0f);
	emitLoop(rInner, PI / sides); // counter-rotated phase for parallax look

	glBindVertexArray(m_pathVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_pathVBO);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
	             verts.data(), GL_DYNAMIC_DRAW);

	glm::mat4 mvp = cam.projectionMatrix(aspect) * cam.viewMatrix();
	m_highlightShader.use();
	m_highlightShader.setMat4("uMVP", mvp);
	m_highlightShader.setMat4("uModel", glm::mat4(1.0f));
	m_highlightShader.setVec3("uSunDir", glm::vec3(0, 1, 0));
	glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
	glUniform1f(glGetUniformLocation(m_highlightShader.id(), "uTintStrength"), 0.0f);
	glUniform1i(glGetUniformLocation(m_highlightShader.id(), "uUseTexture"), 0);
	GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");
	glUniform4f(loc, color.r, color.g, color.b, color.a);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(3.5f);
	glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size() / 3));
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
}

} // namespace civcraft

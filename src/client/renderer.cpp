#include "client/renderer.h"
#include <cmath>
#include <algorithm>

namespace agentica {

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

bool Renderer::init(const std::string& dir) {
	if (!m_terrainShader.loadFromFile(dir + "/terrain.vert", dir + "/terrain.frag"))
		return false;
	if (!m_skyShader.loadFromFile(dir + "/sky.vert", dir + "/sky.frag"))
		return false;
	if (!m_crosshairShader.loadFromFile(dir + "/crosshair.vert", dir + "/crosshair.frag"))
		return false;
	if (!m_highlightShader.loadFromFile(dir + "/highlight.vert", dir + "/highlight.frag"))
		return false;

	// Fullscreen quad for sky
	float quad[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
	glGenVertexArrays(1, &m_skyVAO);
	glGenBuffers(1, &m_skyVBO);
	glBindVertexArray(m_skyVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_skyVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);

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
	for (auto& [_, mesh] : m_meshes) mesh.destroy();
	m_meshes.clear();
	auto del = [](GLuint& vao, GLuint& vbo) {
		if (vbo) glDeleteBuffers(1, &vbo);
		if (vao) glDeleteVertexArrays(1, &vao);
		vao = vbo = 0;
	};
	del(m_skyVAO, m_skyVBO);
	del(m_crosshairVAO, m_crosshairVBO);
	del(m_highlightVAO, m_highlightVBO);
	del(m_crackVAO, m_crackVBO);
	del(m_quadVAO, m_quadVBO);
	m_modelRenderer.shutdown();
	m_fogOfWar.shutdown();
}

void Renderer::markChunkDirty(ChunkPos pos) {
	// Destroy mesh immediately so it gets rebuilt as a "new" chunk in Phase 2
	// of updateChunks (sorted closest-first, so player-adjacent chunks rebuild first).
	auto it = m_meshes.find(pos);
	if (it != m_meshes.end()) {
		it->second.destroy();
		m_meshes.erase(it);
	}
	m_dirtyChunks.erase(pos);
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
				auto verts = m_mesher.buildMesh(world, cp);
				ChunkMesh mesh;
				mesh.pos = cp;
				if (!verts.empty()) mesh.upload(verts);
				m_meshes[cp] = mesh;
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

		auto verts = m_mesher.buildMesh(world, pc.pos);
		ChunkMesh mesh;
		mesh.pos = pc.pos;
		if (!verts.empty()) mesh.upload(verts);
		m_meshes[pc.pos] = mesh;
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

		auto verts = m_mesher.buildMesh(world, dp);
		mit->second.destroy();
		if (!verts.empty()) mit->second.upload(verts);
		else mit->second.vertexCount = 0;
		remeshed++;
	}

	// --- Phase 4: Unload distant meshes + world chunks ---
	std::vector<ChunkPos> toRemove;
	int unloadDistSq = (renderDistance + 2) * (renderDistance + 2);
	for (auto& [pos, mesh] : m_meshes) {
		int dx = pos.x - center.x, dz = pos.z - center.z;
		if (dx*dx + dz*dz > unloadDistSq) {
			mesh.destroy();
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
	glDisable(GL_DEPTH_TEST);
	m_skyShader.use();
	m_skyShader.setMat4("uInvVP", glm::inverse(cam.projectionMatrix(aspect) * cam.viewMatrix()));
	m_skyShader.setVec3("uSkyColor", m_skyColor);
	m_skyShader.setVec3("uHorizonColor", m_horizonColor);
	m_skyShader.setVec3("uSunDir", m_sunDir);
	m_skyShader.setFloat("uSunStrength", m_sunStrength);
	glBindVertexArray(m_skyVAO);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glEnable(GL_DEPTH_TEST);
}

void Renderer::renderTerrain(const Camera& cam, float aspect) {
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	m_terrainShader.use();
	glm::mat4 view = cam.viewMatrix();
	glm::mat4 proj = cam.projectionMatrix(aspect);
	m_terrainShader.setMat4("uView", view);
	m_terrainShader.setMat4("uProj", proj);
	m_terrainShader.setVec3("uSunDir", m_sunDir);
	m_terrainShader.setVec3("uCamPos", cam.position);
	m_terrainShader.setVec3("uFogColor", m_horizonColor);
	m_terrainShader.setFloat("uFogStart", m_fogStart);
	m_terrainShader.setFloat("uFogEnd", m_fogEnd);
	m_terrainShader.setFloat("uSunStrength", m_sunStrength);

	// Frustum culling: skip chunks outside the camera view
	Frustum frustum;
	frustum.extract(proj * view);

	// All blocks are opaque — no blending needed
	for (auto& [pos, mesh] : m_meshes) {
		if (mesh.vertexCount == 0) continue;
		glm::vec3 mn = glm::vec3(pos.x * CHUNK_SIZE, pos.y * CHUNK_SIZE, pos.z * CHUNK_SIZE);
		glm::vec3 mx = mn + glm::vec3(CHUNK_SIZE);
		if (!frustum.testAABB(mn, mx)) continue;
		mesh.draw();
	}

	glDisable(GL_CULL_FACE);
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

void Renderer::renderHotbar(float aspect, int selectedSlot, int hotbarSize) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_highlightShader.use();

	// Hotbar layout: centered at bottom of screen
	float slotSize = 0.06f;
	float padding = 0.008f;
	float totalWidth = hotbarSize * (slotSize + padding) - padding;
	float startX = -totalWidth / 2.0f;
	float startY = -0.92f;

	// Block colors for hotbar preview -- hardcoded for now,
	// will be driven by registry when renderer gets a World reference
	static const glm::vec3 hotbarColors[] = {
		{0.48f,0.48f,0.50f}, // stone
		{0.52f,0.34f,0.20f}, // dirt
		{0.30f,0.58f,0.18f}, // grass
		{0.82f,0.77f,0.50f}, // sand
		{0.42f,0.28f,0.12f}, // wood
		{0.18f,0.48f,0.10f}, // leaves
		{0.93f,0.95f,0.97f}, // snow
	};

	glBindVertexArray(m_quadVAO);

	for (int i = 0; i < hotbarSize; i++) {
		float x = startX + i * (slotSize + padding);
		float y = startY;

		// Scale/translate the unit quad to slot position
		glm::mat4 model(1.0f);
		model = glm::translate(model, glm::vec3(x / aspect, y, 0.0f));
		// Note: divide x by aspect since screen coords are in NDC
		// Actually, we're working in NDC space directly
		model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
		model = glm::scale(model, glm::vec3(slotSize, slotSize * aspect, 1.0f));

		// Slot background
		m_highlightShader.setMat4("uMVP", model);
		GLint loc = glGetUniformLocation(m_highlightShader.id(), "uColor");

		if (i == selectedSlot) {
			// Selected: bright white border
			glUniform4f(loc, 1.0f, 1.0f, 1.0f, 0.75f);
		} else {
			// Normal: dark translucent background
			glUniform4f(loc, 0.08f, 0.08f, 0.08f, 0.50f);
		}
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// Block color preview (smaller inner quad)
		float inset = 0.15f;
		glm::mat4 inner = glm::translate(glm::mat4(1.0f),
			glm::vec3(x + slotSize * inset, y + slotSize * aspect * inset, 0.0f));
		inner = glm::scale(inner,
			glm::vec3(slotSize * (1.0f - 2*inset), slotSize * aspect * (1.0f - 2*inset), 1.0f));

		m_highlightShader.setMat4("uMVP", inner);
		glm::vec3 c = (i < 7) ? hotbarColors[i] : glm::vec3(1, 0, 1);
		glUniform4f(loc, c.r, c.g, c.b, 0.9f);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void Renderer::renderCrosshair(float aspect, glm::vec2 center) {
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_crosshairShader.use();
	m_crosshairShader.setFloat("uAspect", aspect);
	m_crosshairShader.setVec2("uCenter", center);

	glBindVertexArray(m_crosshairVAO);

	// Draw the 4 line arms (8 verts = 4 lines)
	// White with black outline effect: draw black slightly thicker first, then white
	m_crosshairShader.setVec4("uColor", {0, 0, 0, 0.6f});
	glLineWidth(3.0f);
	glDrawArrays(GL_LINES, 0, 8);

	m_crosshairShader.setVec4("uColor", {1, 1, 1, 0.9f});
	glLineWidth(1.5f);
	glDrawArrays(GL_LINES, 0, 8);

	// Draw center dot (6 verts = 2 triangles, starting at vertex 8)
	m_crosshairShader.setVec4("uColor", {1, 1, 1, 0.85f});
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

} // namespace agentica

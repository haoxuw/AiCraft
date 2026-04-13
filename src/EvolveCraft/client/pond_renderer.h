#pragma once

// PondRenderer — renders the pond + all creatures + all food.
// Stage 8: creatures are assembled from procedural parts. We group every
// part instance across all creatures into three buckets by primitive shape
// (sphere / cone / cylinder) and issue one instanced draw per bucket.
// At 5000 cells × ~5 parts, that is 25k instances total in 3 draw calls.

#include "client/gl.h"
#include "client/shader.h"

#include "shared/creature.h"
#include "shared/food.h"
#include "shared/swim_field.h"
#include "shared/body_plan.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

namespace evolvecraft {

class PondRenderer {
public:
	bool init() {
		if (!m_pondShader.loadFromFile("shaders/pond.vert", "shaders/pond.frag")) return false;
		if (!m_partShader.loadFromFile("shaders/part.vert", "shaders/part.frag")) return false;
		if (!m_foodShader.loadFromFile("shaders/food.vert", "shaders/food.frag")) return false;

		buildPondMesh(140.0f);
		buildSphereMesh(m_sphereMesh, 18, 12);
		buildConeMesh(m_coneMesh, 14);
		buildCylinderMesh(m_cylMesh, 12);
		buildSphereMesh(m_foodMesh, 10, 8);

		glGenBuffers(1, &m_sphereInstBuf);
		glGenBuffers(1, &m_coneInstBuf);
		glGenBuffers(1, &m_cylInstBuf);
		glGenBuffers(1, &m_foodInstBuf);

		setupInstancedVAO(m_sphereVAO, m_sphereMesh, m_sphereInstBuf);
		setupInstancedVAO(m_coneVAO,   m_coneMesh,   m_coneInstBuf);
		setupInstancedVAO(m_cylVAO,    m_cylMesh,    m_cylInstBuf);
		setupFoodVAO();
		return true;
	}

	void resize(int w, int h) { m_w = w; m_h = h; }

	void render(const SwimField& field,
	            const std::vector<Creature>& creatures,
	            const std::vector<Food>& foods,
	            const glm::mat4& view, const glm::mat4& proj,
	            const glm::vec3& cameraPos, float time) {
		glEnable(GL_DEPTH_TEST);
		glClearColor(0.03f, 0.05f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Pond
		m_pondShader.use();
		m_pondShader.setMat4("uView", view);
		m_pondShader.setMat4("uProj", proj);
		m_pondShader.setFloat("uRadius", field.radius);
		m_pondShader.setFloat("uTime", time);
		m_pondShader.setVec3("uCamera", cameraPos);
		glBindVertexArray(m_pondVAO);
		glDrawElements(GL_TRIANGLES, m_pondIndices, GL_UNSIGNED_INT, 0);

		// Collect part instances
		m_sphereInsts.clear();
		m_coneInsts.clear();
		m_cylInsts.clear();
		for (auto& c : creatures) {
			if (!c.alive) continue;
			emitTorsoAndParts(c, time);
		}

		if (!m_sphereInsts.empty()) drawInstances(m_sphereVAO, m_sphereInstBuf, m_sphereMesh,
		                                          m_sphereInsts, view, proj, cameraPos, time);
		if (!m_coneInsts.empty())   drawInstances(m_coneVAO,   m_coneInstBuf,   m_coneMesh,
		                                          m_coneInsts,   view, proj, cameraPos, time);
		if (!m_cylInsts.empty())    drawInstances(m_cylVAO,    m_cylInstBuf,    m_cylMesh,
		                                          m_cylInsts,    view, proj, cameraPos, time);

		// Food
		if (!foods.empty()) {
			m_foodInstData.clear();
			for (auto& f : foods) {
				if (!f.alive) continue;
				FoodInstance fi;
				fi.pos = f.pos;
				fi.radius = f.radius;
				fi.bob = f.bobPhase;
				m_foodInstData.push_back(fi);
			}
			if (!m_foodInstData.empty()) {
				glBindBuffer(GL_ARRAY_BUFFER, m_foodInstBuf);
				glBufferData(GL_ARRAY_BUFFER,
				             m_foodInstData.size() * sizeof(FoodInstance),
				             m_foodInstData.data(), GL_DYNAMIC_DRAW);
				m_foodShader.use();
				m_foodShader.setMat4("uView", view);
				m_foodShader.setMat4("uProj", proj);
				m_foodShader.setVec3("uCamera", cameraPos);
				m_foodShader.setFloat("uTime", time);
				glBindVertexArray(m_foodVAO);
				glDrawElementsInstanced(GL_TRIANGLES, m_foodMesh.indexCount,
				                        GL_UNSIGNED_INT, 0,
				                        (GLsizei)m_foodInstData.size());
			}
		}

		glBindVertexArray(0);
	}

private:
	struct MeshHandle {
		GLuint vao = 0, vbo = 0, ibo = 0;
		GLsizei indexCount = 0;
	};
	struct PartInstance {
		glm::mat4 model;   // 4 columns — mapped to attrib slots 2..5
		glm::vec3 color;
		float     _pad = 0.0f;
	};
	struct FoodInstance {
		glm::vec3 pos;
		float     radius;
		float     bob;
		float     _pad = 0.0f;
	};

	// Mesh + pond state
	GLuint m_pondVAO = 0, m_pondVBO = 0, m_pondIBO = 0;
	int m_pondIndices = 0;
	MeshHandle m_sphereMesh, m_coneMesh, m_cylMesh, m_foodMesh;
	GLuint m_sphereVAO = 0, m_coneVAO = 0, m_cylVAO = 0, m_foodVAO = 0;
	GLuint m_sphereInstBuf = 0, m_coneInstBuf = 0, m_cylInstBuf = 0, m_foodInstBuf = 0;

	modcraft::Shader m_pondShader, m_partShader, m_foodShader;

	std::vector<PartInstance> m_sphereInsts, m_coneInsts, m_cylInsts;
	std::vector<FoodInstance> m_foodInstData;
	int m_w = 1280, m_h = 720;

	void drawInstances(GLuint vao, GLuint ibuf, const MeshHandle& mesh,
	                   const std::vector<PartInstance>& insts,
	                   const glm::mat4& view, const glm::mat4& proj,
	                   const glm::vec3& camPos, float time) {
		glBindBuffer(GL_ARRAY_BUFFER, ibuf);
		glBufferData(GL_ARRAY_BUFFER,
		             insts.size() * sizeof(PartInstance), insts.data(), GL_DYNAMIC_DRAW);
		m_partShader.use();
		m_partShader.setMat4("uView", view);
		m_partShader.setMat4("uProj", proj);
		m_partShader.setVec3("uCamera", camPos);
		m_partShader.setFloat("uTime", time);
		glBindVertexArray(vao);
		glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0,
		                        (GLsizei)insts.size());
	}

	// Compose torso + parts into instance buffers.
	// creature.yaw orients the body on XZ; Y axis is world up. Parts are authored
	// in local space with +Z = forward, +Y = up, +X = right; we rotate the creature
	// around world Y by (yaw + pi/2) to map local +Z to the velocity direction.
	void emitTorsoAndParts(const Creature& c, float time) {
		float heading = c.yaw;            // world-space facing
		glm::mat4 R = glm::rotate(glm::mat4(1.0f), -heading, glm::vec3(0, 1, 0));
		glm::mat4 T = glm::translate(glm::mat4(1.0f), c.pos);
		glm::mat4 bodyMat = T * R;

		float size = c.dna.size;
		// Torso — scaled sphere (the ovoid)
		{
			glm::mat4 S = glm::scale(glm::mat4(1.0f), c.body.torsoScale * size);
			PartInstance pi;
			pi.model = bodyMat * S;
			pi.color = c.body.torsoColor;
			m_sphereInsts.push_back(pi);
		}
		// Parts
		for (int i = 0; i < c.body.partCount; i++) {
			const auto& p = c.body.parts[i];
			glm::mat4 M = bodyMat;
			M = glm::translate(M, p.offset * size);
			// yaw then pitch in local space
			if (std::fabs(p.yaw)   > 1e-4f) M = glm::rotate(M, p.yaw,   glm::vec3(0, 1, 0));
			if (std::fabs(p.pitch) > 1e-4f) M = glm::rotate(M, p.pitch, glm::vec3(1, 0, 0));
			// wiggle animation for flagella/tails
			if (p.wiggle > 0.01f) {
				float w = std::sin(time * 8.0f + c.bobPhase) * 0.35f * p.wiggle;
				M = glm::rotate(M, w, glm::vec3(0, 1, 0));
			}
			M = glm::scale(M, p.scale * size);

			PartInstance pi;
			pi.model = M;
			pi.color = p.color;
			switch (p.shape) {
			case PartShape::Sphere:   m_sphereInsts.push_back(pi); break;
			case PartShape::Cone:     m_coneInsts.push_back(pi);   break;
			case PartShape::Cylinder: m_cylInsts.push_back(pi);    break;
			}
		}
	}

	static glm::vec3 hsvToRgb(float h, float s, float v) {
		float c = v * s;
		float x = c * (1 - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1));
		float m = v - c;
		float r=0,g=0,b=0;
		if (h < 1.0f/6)      { r=c; g=x; }
		else if (h < 2.0f/6) { r=x; g=c; }
		else if (h < 3.0f/6) { g=c; b=x; }
		else if (h < 4.0f/6) { g=x; b=c; }
		else if (h < 5.0f/6) { r=x; b=c; }
		else                 { r=c; b=x; }
		return { r+m, g+m, b+m };
	}

	// ------- mesh builders -------

	void buildPondMesh(float outer) {
		const int rings = 64;
		const int sectors = 96;
		std::vector<glm::vec3> verts;
		std::vector<uint32_t>  indices;
		verts.reserve(rings * sectors + 1);
		verts.push_back({0, 0, 0});
		for (int r = 1; r <= rings; r++) {
			float t = (float)r / rings;
			float radius = outer * t * t;
			for (int s = 0; s < sectors; s++) {
				float a = 6.2831853f * s / sectors;
				verts.push_back({ radius * std::cos(a), 0, radius * std::sin(a) });
			}
		}
		for (int s = 0; s < sectors; s++) {
			indices.push_back(0);
			indices.push_back(1 + s);
			indices.push_back(1 + (s + 1) % sectors);
		}
		for (int r = 1; r < rings; r++) {
			int base0 = 1 + (r - 1) * sectors;
			int base1 = 1 + r * sectors;
			for (int s = 0; s < sectors; s++) {
				int s1 = (s + 1) % sectors;
				indices.push_back(base0 + s);
				indices.push_back(base1 + s);
				indices.push_back(base1 + s1);
				indices.push_back(base0 + s);
				indices.push_back(base1 + s1);
				indices.push_back(base0 + s1);
			}
		}
		glGenVertexArrays(1, &m_pondVAO);
		glBindVertexArray(m_pondVAO);
		glGenBuffers(1, &m_pondVBO);
		glBindBuffer(GL_ARRAY_BUFFER, m_pondVBO);
		glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
		glGenBuffers(1, &m_pondIBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_pondIBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
		m_pondIndices = (int)indices.size();
	}

	void buildSphereMesh(MeshHandle& h, int lon, int lat) {
		std::vector<float> verts;
		std::vector<uint32_t> idx;
		for (int y = 0; y <= lat; y++) {
			float v = (float)y / lat;
			float phi = v * 3.14159265f;
			for (int x = 0; x <= lon; x++) {
				float u = (float)x / lon;
				float theta = u * 6.2831853f;
				float px = std::sin(phi) * std::cos(theta);
				float py = std::cos(phi);
				float pz = std::sin(phi) * std::sin(theta);
				verts.push_back(px); verts.push_back(py); verts.push_back(pz);
				verts.push_back(px); verts.push_back(py); verts.push_back(pz);
			}
		}
		for (int y = 0; y < lat; y++) {
			for (int x = 0; x < lon; x++) {
				uint32_t i0 = y * (lon + 1) + x;
				uint32_t i1 = i0 + 1;
				uint32_t i2 = i0 + (lon + 1);
				uint32_t i3 = i2 + 1;
				idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
				idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
			}
		}
		uploadMesh(h, verts, idx);
	}

	// Cone: base at z=0 radius 1, apex at z=1. Normals approximate the side.
	void buildConeMesh(MeshHandle& h, int segments) {
		std::vector<float> verts;
		std::vector<uint32_t> idx;
		// Apex
		verts.insert(verts.end(), {0, 0, 1,  0, 0, 1});
		int apexIdx = 0;
		// Base ring
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			float cx = std::cos(a), cz = std::sin(a);
			// Side normal (points outward + slightly up)
			glm::vec3 n = glm::normalize(glm::vec3(cx, 0.6f, cz));
			verts.insert(verts.end(), {cx, 0, cz,  n.x, n.y, n.z});
		}
		// Side triangles
		for (int s = 0; s < segments; s++) {
			idx.push_back(apexIdx);
			idx.push_back(1 + s);
			idx.push_back(2 + s);
		}
		// Base cap
		int baseCenterIdx = (int)(verts.size() / 6);
		verts.insert(verts.end(), {0, 0, 0,  0, -1, 0});
		int baseRingStart = baseCenterIdx + 1;
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			float cx = std::cos(a), cz = std::sin(a);
			verts.insert(verts.end(), {cx, 0, cz,  0, -1, 0});
		}
		for (int s = 0; s < segments; s++) {
			idx.push_back(baseCenterIdx);
			idx.push_back(baseRingStart + s + 1);
			idx.push_back(baseRingStart + s);
		}
		uploadMesh(h, verts, idx);
	}

	// Cylinder: radius 1, from z=0 to z=1, smooth side normals.
	void buildCylinderMesh(MeshHandle& h, int segments) {
		std::vector<float> verts;
		std::vector<uint32_t> idx;
		// Side verts
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			float cx = std::cos(a), cz = std::sin(a);
			// bottom
			verts.insert(verts.end(), {cx, 0, cz,  cx, 0, cz});
			// top
			verts.insert(verts.end(), {cx, 0, cz + 0,  cx, 0, cz}); // placeholder -- overwritten below
		}
		// Actually populate cleanly: rebuild
		verts.clear();
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			float cx = std::cos(a), cz = std::sin(a);
			verts.insert(verts.end(), {cx, 0, cz,  cx, 0, cz}); // bottom
			verts.insert(verts.end(), {cx, 0, cz,  cx, 0, cz}); // top (will bump z below)
		}
		// Fix top z=1
		for (int s = 0; s <= segments; s++) {
			int topIdx = (s * 2 + 1) * 6;
			verts[topIdx + 2] = 1.0f; // z of pos
			// normal stays (cx, 0, cz)
		}
		for (int s = 0; s < segments; s++) {
			int i0 = s * 2;         // bottom s
			int i1 = s * 2 + 1;     // top s
			int i2 = (s + 1) * 2;   // bottom s+1
			int i3 = (s + 1) * 2 + 1;
			idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
			idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
		}
		// Caps: top (z=1) centered + bottom (z=0) centered
		int topCenterIdx = (int)(verts.size() / 6);
		verts.insert(verts.end(), {0, 0, 1,  0, 0, 1});
		int topCenterRing = topCenterIdx + 1;
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			verts.insert(verts.end(), {std::cos(a), 0, 1,  0, 0, 1});
		}
		for (int s = 0; s < segments; s++) {
			idx.push_back(topCenterIdx);
			idx.push_back(topCenterRing + s);
			idx.push_back(topCenterRing + s + 1);
		}
		int botCenterIdx = (int)(verts.size() / 6);
		verts.insert(verts.end(), {0, 0, 0,  0, 0, -1});
		int botCenterRing = botCenterIdx + 1;
		for (int s = 0; s <= segments; s++) {
			float a = 6.2831853f * s / segments;
			verts.insert(verts.end(), {std::cos(a), 0, 0,  0, 0, -1});
		}
		for (int s = 0; s < segments; s++) {
			idx.push_back(botCenterIdx);
			idx.push_back(botCenterRing + s + 1);
			idx.push_back(botCenterRing + s);
		}
		uploadMesh(h, verts, idx);
	}

	void uploadMesh(MeshHandle& h, const std::vector<float>& verts,
	                const std::vector<uint32_t>& idx) {
		glGenVertexArrays(1, &h.vao);
		glBindVertexArray(h.vao);
		glGenBuffers(1, &h.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, h.vbo);
		glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
		glGenBuffers(1, &h.ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, h.ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
		h.indexCount = (GLsizei)idx.size();
	}

	// ------- VAO setup for instanced part drawing -------

	void setupInstancedVAO(GLuint& vao, const MeshHandle& mesh, GLuint instBuf) {
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);

		glBindBuffer(GL_ARRAY_BUFFER, instBuf);
		// Four mat4 columns at locs 2..5
		for (int c = 0; c < 4; c++) {
			glEnableVertexAttribArray(2 + c);
			glVertexAttribPointer(2 + c, 4, GL_FLOAT, GL_FALSE, sizeof(PartInstance),
			                      (void*)(offsetof(PartInstance, model) + sizeof(float) * 4 * c));
			glVertexAttribDivisor(2 + c, 1);
		}
		// Color at loc 6
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(PartInstance),
		                      (void*)offsetof(PartInstance, color));
		glVertexAttribDivisor(6, 1);
	}

	void setupFoodVAO() {
		glGenVertexArrays(1, &m_foodVAO);
		glBindVertexArray(m_foodVAO);
		glBindBuffer(GL_ARRAY_BUFFER, m_foodMesh.vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_foodMesh.ibo);

		glBindBuffer(GL_ARRAY_BUFFER, m_foodInstBuf);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(FoodInstance),
		                      (void*)offsetof(FoodInstance, pos));
		glVertexAttribDivisor(2, 1);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(FoodInstance),
		                      (void*)offsetof(FoodInstance, radius));
		glVertexAttribDivisor(3, 1);
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(FoodInstance),
		                      (void*)offsetof(FoodInstance, bob));
		glVertexAttribDivisor(4, 1);
	}
};

} // namespace evolvecraft

#include "client/floating_text.h"
#include "client/text.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace agentica {

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────

bool FloatingTextSystem::worldToNDC(const Camera& cam, float aspect,
                                     glm::vec3 wp, glm::vec2& out) const {
	glm::mat4 vp = cam.projectionMatrix(aspect) * cam.viewMatrix();
	glm::vec4 clip = vp * glm::vec4(wp, 1.0f);
	if (clip.w <= 0.001f) return false;
	glm::vec3 ndc = glm::vec3(clip) / clip.w;
	if (ndc.z > 1.0f) return false;  // behind near plane
	out = glm::vec2(ndc.x, ndc.y);
	return true;
}

int FloatingTextSystem::nextHudSlot() const {
	// Find lowest slot index not currently occupied
	bool used[kMaxHudSlots] = {};
	for (auto& e : m_entries) {
		if (e.hudSlot >= 0 && e.hudSlot < kMaxHudSlots)
			used[e.hudSlot] = true;
	}
	for (int i = 0; i < kMaxHudSlots; i++)
		if (!used[i]) return i;
	return kMaxHudSlots - 1;  // overflow: reuse top slot
}

// ─────────────────────────────────────────────────────────────
//  add()
// ─────────────────────────────────────────────────────────────

void FloatingTextSystem::add(FloatTextEvent ev) {
	// Coalesce: merge with existing entry of same key + type
	if (!ev.coalesceKey.empty()) {
		for (auto& e : m_entries) {
			if (e.type == ev.type && e.coalesceKey == ev.coalesceKey &&
			    e.entityId == ev.targetId) {
				// Replace text, refresh timer
				e.text = ev.text;
				e.ttl  = std::min(e.maxTtl, e.ttl + kHudTtl * 0.5f);
				return;
			}
		}
	}

	bool isHud = (ev.targetId == ENTITY_NONE ||
	              ev.type == FloatTextType::DamageTaken ||
	              ev.type == FloatTextType::Pickup ||
	              ev.type == FloatTextType::BlockBreak);

	Entry entry;
	entry.type         = ev.type;
	entry.entityId     = ev.targetId;
	entry.anchorWorld  = ev.worldPos;
	entry.screenDrift  = {0.0f, 0.0f};
	entry.text         = ev.text;
	entry.coalesceKey  = ev.coalesceKey;
	entry.color        = ev.color;
	entry.scale        = 0.0f;  // pop-in starts at 0
	entry.hudSlot      = -1;

	if (isHud) {
		entry.maxTtl = kHudTtl;
		entry.ttl    = kHudTtl;
		entry.hudSlot = nextHudSlot();
	} else {
		// Entity-anchored: limit to kMaxEntitySlots per entity
		int count = 0;
		for (auto& e : m_entries)
			if (e.entityId == ev.targetId && e.hudSlot < 0) count++;
		if (count >= kMaxEntitySlots) {
			// Drop oldest entity entry for this entity
			for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
				if (it->entityId == ev.targetId && it->hudSlot < 0) {
					m_entries.erase(it);
					break;
				}
			}
		}
		entry.maxTtl = kEntityTtl;
		entry.ttl    = kEntityTtl;
	}

	m_entries.push_back(std::move(entry));
}

// ─────────────────────────────────────────────────────────────
//  update()
// ─────────────────────────────────────────────────────────────

void FloatingTextSystem::update(float dt, CameraMode /*mode*/) {
	for (auto& e : m_entries) {
		e.ttl -= dt;

		// Pop-in: scale from 0→1 quickly
		float elapsed = e.maxTtl - e.ttl;
		if (elapsed < kPopInTime) {
			e.scale = elapsed / kPopInTime;
		} else {
			e.scale = 1.0f;
		}

		// Entity-anchored entries drift upward in screen space
		if (e.hudSlot < 0) {
			e.screenDrift.y += dt * 0.30f;  // NDC units per second
		}
	}

	// Remove expired entries
	m_entries.erase(
		std::remove_if(m_entries.begin(), m_entries.end(),
			[](const Entry& e) { return e.ttl <= 0.0f; }),
		m_entries.end()
	);
}

// ─────────────────────────────────────────────────────────────
//  render()
// ─────────────────────────────────────────────────────────────

void FloatingTextSystem::render(const Camera& cam, float aspect, CameraMode mode,
                                 TextRenderer& text,
                                 const std::vector<EntityId>& selectedEntities) {
	// ── HUD lane (left column, fixed positions) ──
	// Slot 0 is lowest, slots stack upward.
	// Position: x = -0.92, y = -0.70 + slot * 0.12
	for (auto& e : m_entries) {
		if (e.hudSlot < 0) continue;

		float fadeIn  = std::min((e.maxTtl - e.ttl) / kPopInTime, 1.0f);
		float fadeOut = std::min(e.ttl / 0.4f, 1.0f);
		float alpha   = e.color.a * fadeIn * fadeOut;
		if (alpha < 0.01f) continue;

		float x = -0.96f;
		float y = -0.70f + e.hudSlot * 0.12f;
		float sc = 0.55f * e.scale;

		glm::vec4 col = e.color;
		col.a = alpha;
		text.drawText(e.text, x, y, sc, col, aspect);
	}

	// ── Entity-anchored entries ──
	for (auto& e : m_entries) {
		if (e.hudSlot >= 0) continue;
		if (e.entityId == ENTITY_NONE) continue;

		// RTS mode: only show for selected entities
		if (mode == CameraMode::RTS &&
		    std::find(selectedEntities.begin(), selectedEntities.end(), e.entityId) == selectedEntities.end())
			continue;

		// FPS mode: show DamageDealt near crosshair instead of world-projected
		if (mode == CameraMode::FirstPerson) {
			float fadeIn  = std::min((e.maxTtl - e.ttl) / kPopInTime, 1.0f);
			float fadeOut = std::min(e.ttl / 0.4f, 1.0f);
			float alpha   = e.color.a * fadeIn * fadeOut;
			if (alpha < 0.01f) continue;

			// Cluster near top of crosshair, drift upward
			float x = -0.06f + e.screenDrift.x;
			float y =  0.08f + e.screenDrift.y;
			float sc = 0.60f * e.scale;

			glm::vec4 col = e.color;
			col.a = alpha;
			text.drawText(e.text, x, y, sc, col, aspect);
			continue;
		}

		// TPS / RPG / RTS: project world anchor + drift
		glm::vec2 ndc;
		glm::vec3 wp = e.anchorWorld + glm::vec3(0.0f, 0.2f, 0.0f);
		if (!worldToNDC(cam, aspect, wp, ndc)) continue;

		float fadeIn  = std::min((e.maxTtl - e.ttl) / kPopInTime, 1.0f);
		float fadeOut = std::min(e.ttl / 0.5f, 1.0f);
		float alpha   = e.color.a * fadeIn * fadeOut;
		if (alpha < 0.01f) continue;

		// Apply accumulated upward screen drift
		float x = ndc.x - 0.04f;
		float y = ndc.y + e.screenDrift.y;
		float sc = 0.65f * e.scale;

		glm::vec4 col = e.color;
		col.a = alpha;
		text.drawText(e.text, x, y, sc, col, aspect);
	}
}

} // namespace agentica

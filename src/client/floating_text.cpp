#include "client/floating_text.h"
#include "client/text.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace agentica {

// ─────────────────────────────────────────────────────────────────────────────
//  Small helpers
// ─────────────────────────────────────────────────────────────────────────────

float FloatingTextManager::ttlFor(FloatSource src) const {
	return (src == FloatSource::Pickup || src == FloatSource::BlockBreak)
	       ? kPickupTtl : kCombatTtl;
}

bool FloatingTextManager::worldToNDC(const Camera& cam, float aspect,
                                     glm::vec3 wp, glm::vec2& out) const {
	glm::mat4 vp = cam.projectionMatrix(aspect) * cam.viewMatrix();
	glm::vec4 clip = vp * glm::vec4(wp, 1.0f);
	if (clip.w <= 0.001f) return false;
	glm::vec3 ndc = glm::vec3(clip) / clip.w;
	if (ndc.z > 1.0f) return false;
	out = {ndc.x, ndc.y};
	return true;
}

std::string FloatingTextManager::formatDisplay(FloatSource src, float accum,
                                               const std::string& label) const {
	int v = (int)accum;
	switch (src) {
	case FloatSource::DamageDealt:
	case FloatSource::DamageTaken:
		return "-" + std::to_string(v);
	case FloatSource::Heal:
		return "+" + std::to_string(v) + " HP";
	case FloatSource::Pickup: {
		std::string prefix = (v >= 0) ? "+" : "";  // negative already carries "-"
		return prefix + std::to_string(v) + (label.empty() ? "" : " " + label);
	}
	case FloatSource::BlockBreak:
		return label + (v > 1 ? " x" + std::to_string(v) : "");
	}
	return label;
}

glm::vec4 FloatingTextManager::colorFor(FloatSource src, bool isCrit, bool isDying) const {
	switch (src) {
	case FloatSource::DamageDealt:
		if (isDying)  return {1.0f, 0.20f, 0.10f, 1.0f}; // red   — kill
		if (isCrit)   return {1.0f, 0.55f, 0.10f, 1.0f}; // orange — crit
		return                {1.0f, 0.85f, 0.10f, 1.0f}; // yellow — normal
	case FloatSource::DamageTaken:
		return {1.0f, 0.25f, 0.15f, 1.0f};  // vivid red
	case FloatSource::Heal:
		return {0.25f, 1.0f, 0.40f, 1.0f};  // green
	case FloatSource::Pickup:
		return {0.85f, 1.0f, 0.55f, 1.0f};  // lime
	case FloatSource::BlockBreak:
		return {0.70f, 0.70f, 0.70f, 1.0f}; // gray
	}
	return {1, 1, 1, 1};
}

// Single-pass NDC push-apart. Modifies the temporary position vector, not entries.
void FloatingTextManager::resolveOverlap(std::vector<glm::vec2>& positions) const {
	for (int i = 0; i < (int)positions.size(); i++) {
		for (int j = i + 1; j < (int)positions.size(); j++) {
			glm::vec2 delta = positions[j] - positions[i];
			float dist = glm::length(delta);
			if (dist > 0.001f && dist < kOverlapDist) {
				glm::vec2 push = (delta / dist) * (kOverlapDist - dist) * 0.5f;
				positions[i] -= push;
				positions[j] += push;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  add()
// ─────────────────────────────────────────────────────────────────────────────

void FloatingTextManager::add(const FloatTextEvent& ev) {
	// ── Splash: brief per-hit flash (independent of Counter) ──────────────
	if (ev.isSplash && (ev.source == FloatSource::DamageDealt ||
	                    ev.source == FloatSource::DamageTaken  ||
	                    ev.source == FloatSource::Heal)) {
		Splash s;
		s.source      = ev.source;
		s.entityId    = ev.targetId;
		s.anchorWorld = ev.worldPos;
		s.screenDrift = {0, 0};
		s.text        = formatDisplay(ev.source, ev.value, "");
		s.color       = colorFor(ev.source, ev.isCrit, ev.isDying);
		s.isCrit      = ev.isCrit;
		s.ttl         = kSplashTtl;
		s.scale       = 0.f;
		// Small random horizontal jitter so rapid hits fan out instead of stacking
		s.horizJitter = ((float)(std::rand() % 200) / 100.f - 1.0f) * 0.03f;
		m_splashes.push_back(std::move(s));
	}

	// ── Counter map: accumulate into persistent slot ───────────────────────
	EntryKey key{ ev.targetId, ev.source, ev.coalesceKey };

	auto it = m_entries.find(key);
	if (it != m_entries.end()) {
		Entry& e = it->second;
		e.accum += ev.value;
		e.text   = formatDisplay(e.source, e.accum, e.baseLabel);
		e.animAge = 0.f;       // re-trigger bounce animation on accumulation
		// If the counter nets to zero (e.g. pick up then drop same item), fade it
		// out quickly instead of showing "+0 Wood" for the full TTL.
		e.ttl = ((int)e.accum == 0) ? std::min(e.ttl, kCounterFadeOut) : e.maxTtl;
		// Upgrade severity: crit or dying flags persist if any hit triggered them
		if (ev.isCrit)  e.isCrit = true;
		if (ev.isCrit || ev.isDying)
			e.color = colorFor(e.source, e.isCrit, ev.isDying);
		return;
	}

	// New slot
	Entry e;
	e.source      = ev.source;
	e.entityId    = ev.targetId;
	e.anchorWorld = ev.worldPos;
	e.screenDrift = {0, 0};
	e.baseLabel   = ev.text;
	e.accum       = ev.value;
	e.text        = formatDisplay(ev.source, ev.value, ev.text);
	e.color       = colorFor(ev.source, ev.isCrit, ev.isDying);
	e.isCrit      = ev.isCrit;
	e.freeFloat   = false;
	e.maxTtl      = ttlFor(ev.source);
	e.ttl         = e.maxTtl;
	e.scale       = 0.f;

	m_entries.emplace(key, std::move(e));
}

// ─────────────────────────────────────────────────────────────────────────────
//  onEntityRemoved()
// ─────────────────────────────────────────────────────────────────────────────

void FloatingTextManager::onEntityRemoved(EntityId id) {
	for (auto& [k, e] : m_entries) {
		if (e.entityId == id && !e.freeFloat) {
			e.freeFloat = true; // keep drifting from last known position
		}
	}
	for (auto& s : m_splashes)
		if (s.entityId == id)
			s.entityId = ENTITY_NONE; // detach splash too
}

// ─────────────────────────────────────────────────────────────────────────────
//  update()
// ─────────────────────────────────────────────────────────────────────────────

void FloatingTextManager::update(float dt, CameraMode mode) {
	// Mode switch: combat entries fast-expire
	if (mode != m_prevMode) {
		for (auto& [k, e] : m_entries)
			if (e.ttl > kModeSwitchTtl)
				e.ttl = kModeSwitchTtl;
		m_prevMode = mode;
	}

	// Bounce animation helper: 0 → peak → 1.0, re-triggered by animAge reset
	auto bounceScale = [this](float age) -> float {
		if (age < kPopPeakTime)
			return (age / kPopPeakTime) * kPopPeak;
		if (age < kPopSettleTime) {
			float t = (age - kPopPeakTime) / (kPopSettleTime - kPopPeakTime);
			return kPopPeak - (kPopPeak - 1.0f) * t;
		}
		return 1.0f;
	};

	// Counter entries
	for (auto& [k, e] : m_entries) {
		e.ttl     -= dt;
		e.animAge += dt;
		e.scale    = bounceScale(e.animAge);
		e.screenDrift.y += dt * kDriftRate;
	}
	// Erase expired Counter entries
	for (auto it = m_entries.begin(); it != m_entries.end(); )
		it = (it->second.ttl <= 0.f) ? m_entries.erase(it) : std::next(it);

	// Splash entries (bounce from elapsed time — no separate animAge needed)
	for (auto& s : m_splashes) {
		s.ttl -= dt;
		float age = kSplashTtl - s.ttl;
		s.scale = bounceScale(age);
		s.screenDrift.y += dt * kSplashDriftRate;
	}
	m_splashes.erase(
		std::remove_if(m_splashes.begin(), m_splashes.end(),
			[](const Splash& s) { return s.ttl <= 0.f; }),
		m_splashes.end());
}

// ─────────────────────────────────────────────────────────────────────────────
//  render()
// ─────────────────────────────────────────────────────────────────────────────

void FloatingTextManager::render(const Camera& cam, float aspect, CameraMode mode,
                                 TextRenderer& text,
                                 const std::vector<EntityId>& selectedEntities) {

	auto fadeAlpha = [](float ttl, float maxTtl, float fadeOutSec, float a) -> float {
		float fadeIn  = std::min((maxTtl - ttl) / kPopInTime, 1.0f);
		float fadeOut = std::min(ttl / fadeOutSec, 1.0f);
		return a * fadeIn * fadeOut;
	};

	// Scale for a given entry: crits pop larger; pickup/break slightly smaller than combat.
	auto entryScale = [](const Entry& e) -> float {
		if (e.isCrit) return kFTScaleCrit;
		if (e.source == FloatSource::Pickup || e.source == FloatSource::BlockBreak)
			return kFTScalePickup;
		return kFTScaleWorld;
	};

	// ── Collect all entries and their screen positions ────────────────────────
	struct ScreenEntry {
		const Entry* entry;
		glm::vec2    pos;
		float        scale;
		float        alpha;
	};
	std::vector<ScreenEntry> anchored;

	for (auto& [k, e] : m_entries) {
		// RTS: only selected entities (skip pickup/break which have no entityId)
		if (mode == CameraMode::RTS && e.entityId != ENTITY_NONE) {
			bool isSelected = std::find(selectedEntities.begin(),
			                            selectedEntities.end(),
			                            e.entityId) != selectedEntities.end();
			if (!isSelected) continue;
		}

		float alpha = fadeAlpha(e.ttl, e.maxTtl, kCounterFadeOut, e.color.a);
		if (alpha < 0.01f) continue;

		ScreenEntry se;
		se.entry = &e;
		se.alpha = alpha;
		se.scale = entryScale(e) * e.scale;

		if (mode == CameraMode::FirstPerson) {
			se.pos = {};  // placeholder — re-assigned below in FPS layout pass
			anchored.push_back(se);
		} else {
			// TPS / RPG / RTS: project world anchor to screen
			glm::vec3 wp = e.anchorWorld + glm::vec3(0, kAnchorLiftY, 0);
			glm::vec2 ndc;
			if (!worldToNDC(cam, aspect, wp, ndc)) continue;
			se.pos = ndc + glm::vec2(0, e.screenDrift.y);
			anchored.push_back(se);
		}
	}

	if (mode == CameraMode::FirstPerson) {
		// ── FPS: Counter panel layout ─────────────────────────────────────────
		// Three fixed screen regions, each a stacked list (no drift — text
		// updates in-place like a counter dictionary, fades when TTL expires).
		//   Damage taken   → bottom-centre    (stacks upward)
		//   Damage dealt / Heal → crosshair   (stacks upward)
		//   Pickup / Break → upper-right panel (stacks downward)
		std::vector<ScreenEntry*> taken, dealt, loot;
		for (auto& se : anchored) {
			switch (se.entry->source) {
			case FloatSource::DamageTaken:                                    taken.push_back(&se); break;
			case FloatSource::DamageDealt: case FloatSource::Heal:           dealt.push_back(&se); break;
			case FloatSource::Pickup:      case FloatSource::BlockBreak:     loot.push_back(&se);  break;
			}
		}
		// Sort by stable key (item name string) so row assignments never change
		// between frames. TTL-based sorting caused flickering because many entries
		// share identical TTLs and std::sort is not stable.
		auto byLabel = [](ScreenEntry* a, ScreenEntry* b) {
			return a->entry->baseLabel < b->entry->baseLabel;
		};
		std::sort(taken.begin(), taken.end(), byLabel);
		std::sort(dealt.begin(), dealt.end(), byLabel);
		std::sort(loot.begin(),  loot.end(),  byLabel);

		for (int i = 0; i < (int)taken.size(); i++)
			taken[i]->pos = { kFpsTakenX, kFpsTakenBaseY + i * kFpsRowH };
		for (int i = 0; i < (int)dealt.size(); i++)
			dealt[i]->pos = { kFpsDealtX, kFpsDealtBaseY + i * kFpsRowH };
		for (int i = 0; i < (int)loot.size(); i++)
			loot[i]->pos  = { kFpsLootX,  kFpsLootBaseY  - i * kFpsRowH };
	} else {
		// TPS / RPG / RTS: push-apart to separate world-anchored entries
		std::vector<glm::vec2> positions;
		positions.reserve(anchored.size());
		for (auto& se : anchored) positions.push_back(se.pos);
		resolveOverlap(positions);
		for (int i = 0; i < (int)anchored.size(); i++)
			anchored[i].pos = positions[i];
	}

	// Draw Counter entries
	for (auto& se : anchored) {
		glm::vec4 col = se.entry->color; col.a = se.alpha;
		text.drawText(se.entry->text, se.pos.x, se.pos.y, se.scale, col, aspect);
	}

	// ── Splash entries (per-hit flashes) ─────────────────────────────────────
	for (auto& s : m_splashes) {
		float alpha = fadeAlpha(s.ttl, kSplashTtl, kSplashFadeOut, s.color.a);
		if (alpha < 0.01f) continue;

		float sc = (s.isCrit ? kFTScaleCrit : kFTScaleWorld) * kSplashScaleMul * s.scale;
		glm::vec4 col = s.color; col.a = alpha * kSplashAlphaMul;

		if (mode == CameraMode::FirstPerson) {
			if (s.source == FloatSource::DamageTaken) {
				text.drawText(s.text, s.horizJitter, kFpsSplashDmgY - s.screenDrift.y, sc, col, aspect);
			} else {
				text.drawText(s.text, s.horizJitter, kFpsSplashHitY + s.screenDrift.y, sc, col, aspect);
			}
		} else {
			glm::vec3 wp = s.anchorWorld + glm::vec3(0, kAnchorLiftY, 0);
			glm::vec2 ndc;
			if (!worldToNDC(cam, aspect, wp, ndc)) continue;
			text.drawText(s.text, ndc.x + s.horizJitter, ndc.y + s.screenDrift.y, sc, col, aspect);
		}
	}
}

} // namespace agentica

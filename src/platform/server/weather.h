#pragma once

// Server-owned Markov chain. Broadcasts (kind, intensity, wind) via S_WEATHER.
// Rule 5: visuals are client-side, this file knows nothing about rendering.

#include "server/python_bridge.h"
#include <cmath>
#include <cstdint>
#include <random>
#include <string>

namespace civcraft {

struct WeatherState {
	std::string kind      = "clear";   // WeatherPyConfig::Kind::name
	float       intensity = 0.0f;      // 0..1 — client maps to visuals
	float       windX     = 0.0f;      // world XZ, -1..1
	float       windZ     = 0.0f;
	uint32_t    seq       = 0;         // bumped on kind/intensity change
};

class WeatherController {
public:
	void load(const WeatherPyConfig& cfg, uint32_t seed) {
		m_cfg = cfg;
		m_rng.seed(seed);
		m_state = WeatherState{};
		m_state.kind = resolveInitial(cfg);
		applyIntensity(m_state.kind);
		m_state.seq = 1;
		m_secondsUntilTransition = sampleDuration(m_state.kind);
		m_elapsed = 0.0f;
		m_dirty = true;
	}

	void tick(float dt) {
		m_elapsed += dt;
		m_secondsUntilTransition -= dt;

		// Wind: base + slow sinusoidal noise.
		float phase = (m_cfg.windNoiseScale > 0.01f)
		              ? (m_elapsed / m_cfg.windNoiseScale) * 6.2831853f
		              : 0.0f;
		float noiseX = std::sin(phase) * m_cfg.windNoiseAmp;
		float noiseZ = std::cos(phase * 0.7f) * m_cfg.windNoiseAmp;
		float newWX  = m_cfg.baseWindX + noiseX;
		float newWZ  = m_cfg.baseWindZ + noiseZ;

		// Low-pass so particles don't pop. Wind doesn't bump seq — only kind/intensity do.
		m_state.windX = m_state.windX * 0.98f + newWX * 0.02f;
		m_state.windZ = m_state.windZ * 0.98f + newWZ * 0.02f;

		if (m_secondsUntilTransition <= 0.0f) {
			std::string next = pickNext(m_state.kind);
			if (!next.empty() && next != m_state.kind) {
				m_state.kind = next;
				applyIntensity(next);
				m_state.seq++;
				m_dirty = true;
			}
			m_secondsUntilTransition = sampleDuration(m_state.kind);
		}
	}

	const WeatherState& state() const { return m_state; }

	// True once per kind/intensity mutation; read-and-clear.
	bool checkAndClearDirty() {
		bool d = m_dirty;
		m_dirty = false;
		return d;
	}

	// Debug/admin override; bumps seq so clients observe change.
	void forceKind(const std::string& kind) {
		m_state.kind = kind;
		applyIntensity(kind);
		m_state.seq++;
		m_secondsUntilTransition = sampleDuration(kind);
		m_dirty = true;
	}

private:
	WeatherPyConfig m_cfg;
	WeatherState    m_state;
	std::mt19937    m_rng{0xC17C1A};
	float           m_secondsUntilTransition = 0.0f;
	float           m_elapsed = 0.0f;
	bool            m_dirty = false;

	std::string resolveInitial(const WeatherPyConfig& cfg) const {
		for (const auto& k : cfg.kinds)
			if (k.name == cfg.initialKind) return k.name;
		return cfg.kinds.empty() ? std::string("clear") : cfg.kinds.front().name;
	}

	const WeatherPyConfig::Kind* findKind(const std::string& name) const {
		for (const auto& k : m_cfg.kinds)
			if (k.name == name) return &k;
		return nullptr;
	}

	void applyIntensity(const std::string& name) {
		const auto* k = findKind(name);
		if (!k) { m_state.intensity = 0.0f; return; }
		if (k->maxIntensity <= k->minIntensity) {
			m_state.intensity = k->minIntensity;
			return;
		}
		std::uniform_real_distribution<float> d(k->minIntensity, k->maxIntensity);
		m_state.intensity = d(m_rng);
	}

	// Exponential-ish, E=meanSeconds, clamped [0.2·mean, 3·mean] to avoid stick/flap.
	float sampleDuration(const std::string& name) {
		const auto* k = findKind(name);
		float mean = k ? k->meanSeconds : 120.0f;
		std::uniform_real_distribution<float> u(0.0001f, 0.9999f);
		float s = -std::log(u(m_rng)) * mean;
		return std::clamp(s, 0.2f * mean, 3.0f * mean);
	}

	std::string pickNext(const std::string& from) {
		const auto* k = findKind(from);
		if (!k || k->next.empty()) return from;
		float total = 0.0f;
		for (const auto& [_, w] : k->next) total += std::max(w, 0.0f);
		if (total <= 0.0f) return from;
		std::uniform_real_distribution<float> u(0.0f, total);
		float r = u(m_rng);
		float acc = 0.0f;
		for (const auto& [name, w] : k->next) {
			acc += std::max(w, 0.0f);
			if (r <= acc) return name;
		}
		return from;
	}
};

} // namespace civcraft

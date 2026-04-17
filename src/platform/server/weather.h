#pragma once

// WeatherController — server-owned, tick-driven Markov chain over weather
// kinds. Produces (kind, intensity, wind) state that the ClientManager
// broadcasts to all connected clients via S_WEATHER. Visuals are client-
// side (Rule 5): this file knows nothing about rendering.

#include "server/python_bridge.h"
#include <cmath>
#include <cstdint>
#include <random>
#include <string>

namespace civcraft {

struct WeatherState {
	std::string kind      = "clear";   // matches WeatherPyConfig::Kind::name
	float       intensity = 0.0f;      // 0..1 — client decides visual mapping
	float       windX     = 0.0f;      // world-space wind XZ components (-1..1)
	float       windZ     = 0.0f;
	uint32_t    seq       = 0;         // bumped on every kind/intensity change
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

	// Advance the schedule by dt seconds. Returns true if state mutated
	// since the last call to changed().
	void tick(float dt) {
		m_elapsed += dt;
		m_secondsUntilTransition -= dt;

		// Wind: base vector + slow sinusoidal noise keyed off elapsed time.
		float phase = (m_cfg.windNoiseScale > 0.01f)
		              ? (m_elapsed / m_cfg.windNoiseScale) * 6.2831853f
		              : 0.0f;
		float noiseX = std::sin(phase) * m_cfg.windNoiseAmp;
		float noiseZ = std::cos(phase * 0.7f) * m_cfg.windNoiseAmp;
		float newWX  = m_cfg.baseWindX + noiseX;
		float newWZ  = m_cfg.baseWindZ + noiseZ;

		// Low-pass the wind so direction doesn't flip every broadcast and
		// particle systems don't pop. Wind is NOT counted as a state change
		// for seq purposes — only kind/intensity are (seq is expensive —
		// clients may use it to crossfade particles).
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

	// Returns true once per mutation (kind or intensity change). Resets on
	// read — the broadcaster uses it to decide whether to re-send S_WEATHER.
	bool checkAndClearDirty() {
		bool d = m_dirty;
		m_dirty = false;
		return d;
	}

	// Manual override for debug / admin commands. Bumps seq so clients
	// observe the change.
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

	// Exponential-ish sample: expected value = meanSeconds, clamped to
	// [0.2 * mean, 3 * mean] so the schedule never sticks or flaps.
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

#include "client/loading_screen.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace solarium::vk {

// ── LoadingGate ───────────────────────────────────────────────────────

LoadingGate::LoadingGate() {
	phases[Welcome]       = {"Server handshake",     0.0f};
	phases[WorldPrepared] = {"World preparing",      0.0f};
	phases[ChunksLoaded]  = {"Loading terrain",      0.0f};
	phases[AgentsSettled] = {"Waking up villagers",  0.0f};
}

void LoadingGate::reset() {
	for (auto& p : phases) p.progress = 0.0f;
}

void LoadingGate::set(Id id, float pct) {
	phases[id].progress = std::clamp(pct, 0.0f, 1.0f);
}

bool LoadingGate::allDone() const {
	for (auto& p : phases) if (p.progress < 1.0f) return false;
	return true;
}

float LoadingGate::aggregate() const {
	if (allDone()) return 1.0f;
	float s = 0.0f;
	for (auto& p : phases) s += p.progress;
	return std::min(0.97f, s / (float)phases.size());
}

// ── LoadingScreen ─────────────────────────────────────────────────────

void LoadingScreen::reset() {
	m_gate.reset();
	m_ready               = false;
	m_agentsSettleDisplay = 0.0f;
	m_aggregateDisplay    = 0.0f;
	// Key/mouse edge state is self-healing: the next pollDismiss() rewrites
	// m_last* from the current GLFW state, so a stale held key is absorbed.
}

void LoadingScreen::setWelcome(bool received) {
	m_gate.set(LoadingGate::Welcome, received ? 1.0f : 0.0f);
}

void LoadingScreen::setWorldPrepared(float frac) {
	m_gate.set(LoadingGate::WorldPrepared, frac);
}

void LoadingScreen::setChunkProgress(float streamFrac, bool quiesced) {
	// Cap pre-quiesce at 0.98 so the user never sees a full-looking chunk
	// phase that then lingers on the quiesce timer.
	m_gate.set(LoadingGate::ChunksLoaded,
	           quiesced ? 1.0f : std::min(streamFrac, 0.98f));
}

void LoadingScreen::setAgentProgress(bool discoveryRan, int total,
                                     int settled, float dt) {
	if (!discoveryRan) {
		m_agentsSettleDisplay = 0.0f;
		m_gate.set(LoadingGate::AgentsSettled, 0.0f);
		return;
	}
	if (total == 0) {
		m_agentsSettleDisplay = 1.0f;
		m_gate.set(LoadingGate::AgentsSettled, 1.0f);
		return;
	}
	// Budget kPerAgent seconds per first-decide. Display climbs at that
	// rate, floored at the real settled ratio and capped one slot ahead
	// so it never claims more done than confirmed.
	constexpr float kPerAgent = 0.25f;
	const float slot     = 1.0f / (float)total;
	const float rate     = slot / kPerAgent;
	const float realFrac = (float)settled * slot;
	m_agentsSettleDisplay += rate * dt;
	m_agentsSettleDisplay = std::clamp(
		m_agentsSettleDisplay,
		realFrac,
		std::min(1.0f, realFrac + slot * 0.95f));
	m_gate.set(LoadingGate::AgentsSettled, m_agentsSettleDisplay);
}

void LoadingScreen::tick() {
	if (!m_ready && m_gate.allDone()) m_ready = true;
	const float agg = m_ready ? 1.0f : m_gate.aggregate();
	if (agg > m_aggregateDisplay) m_aggregateDisplay = agg;
}

bool LoadingScreen::pollDismiss(GLFWwindow* w) {
	if (!w) return false;
	bool any = false;
	for (int k = kKeyLo; k <= kKeyHi; ++k) {
		int cur = glfwGetKey(w, k);
		int& prev = m_lastKey[k - kKeyLo];
		if (cur == GLFW_PRESS && prev != GLFW_PRESS) any = true;
		prev = cur;
	}
	int curEnter = glfwGetKey(w, GLFW_KEY_ENTER);
	int curMouse = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT);
	if (curEnter == GLFW_PRESS && m_lastEnter != GLFW_PRESS) any = true;
	if (curMouse == GLFW_PRESS && m_lastMouse != GLFW_PRESS) any = true;
	m_lastEnter = curEnter;
	m_lastMouse = curMouse;
	return any;
}

} // namespace solarium::vk

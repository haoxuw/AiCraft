#pragma once

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace solarium::vk {

// Data-driven readiness gate. Each phase publishes a 0..1 progress; the
// screen only hands off once every phase hits 1.0. Adding or removing a
// phase is a single-line edit — the renderer walks the array in order.
struct LoadingGate {
	struct Phase {
		const char* label    = "";
		float       progress = 0.0f;  // clamped 0..1
	};
	enum Id : uint8_t {
		Welcome,          // S_HELLO / pollWelcome()
		WorldPrepared,    // S_PREPARING 0..100% → S_READY
		ChunksLoaded,     // mesher quiesced within render radius
		AgentsSettled,    // every owned NPC finished its first decide
		kCount
	};
	std::array<Phase, kCount> phases;

	LoadingGate();
	void  reset();
	void  set(Id id, float pct);
	bool  allDone()  const;
	// Full fill is reserved for allDone() so the bar never reads "100%"
	// while any phase is still short.
	float aggregate() const;
};

// Owns the gate plus all the policy that sits on top of it: smoothing,
// sticky ready flag, monotone aggregate, "press any key" edge detection.
// Game feeds per-frame signals via the set*() methods, then calls tick()
// and reads ready() / aggregateDisplay() / gate() for rendering.
class LoadingScreen {
public:
	void reset();

	void setWelcome(bool received);
	void setWorldPrepared(float frac);                       // 1.0 = S_READY
	void setChunkProgress(float streamFrac, bool quiesced);
	void setAgentProgress(bool discoveryRan, int total,
	                      int settled, float dt);
	// Latch ready and advance the monotone aggregate display. Call after
	// all set*() pushes for the frame.
	void tick();

	bool               ready()            const { return m_ready; }
	float              aggregateDisplay() const { return m_aggregateDisplay; }
	const LoadingGate& gate()             const { return m_gate; }

	// Printable-key or LMB edge; returns true exactly once per press.
	bool pollDismiss(GLFWwindow* w);

private:
	LoadingGate m_gate;
	bool  m_ready               = false;
	float m_agentsSettleDisplay = 0.0f;
	float m_aggregateDisplay    = 0.0f;

	static constexpr int kKeyLo = 32;   // GLFW_KEY_SPACE
	static constexpr int kKeyHi = 90;   // GLFW_KEY_Z
	int m_lastKey[kKeyHi - kKeyLo + 1] = {0};
	int m_lastEnter = 0;
	int m_lastMouse = 0;
};

} // namespace solarium::vk

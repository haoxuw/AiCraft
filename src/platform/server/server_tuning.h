#pragma once

// Server tuning constants. All magic numbers live here.

namespace civcraft::ServerTuning {
	constexpr float spawnHeightOffset    = 1.5f;

	// Stuck detection
	constexpr float stuckCheckInterval   = 2.0f;
	constexpr float stuckMinSpeed        = 0.3f;
	constexpr float stuckMaxDisplacement = 0.1f;
	constexpr float unstuckNudgeHeight   = 1.5f;
	constexpr float unstuckNudgeHoriz    = 0.5f;

	constexpr float gravity              = 32.0f; // ~3.2x real g for snappy fall
	constexpr float entityStepHeight     = 1.0f;

	// Server-side greedy steering
	constexpr float navArriveDistance    = 1.2f;
	constexpr float navDodgeAngle        = 0.785f; // 45 deg
	constexpr float navDodgeDuration     = 1.5f;
	constexpr float navStuckTimeout      = 2.0f;
	constexpr float navStuckMinMove      = 0.5f;
	constexpr float navFormationSpacing  = 2.0f;

	constexpr float hpRegenInterval      = 10.0f;
	constexpr float structureRegenCheckInterval = 5.0f;

	constexpr float tickRate             = 1.0f / 60.0f;  // 60 tps
	constexpr float broadcastInterval    = 0.05f;          // 20 Hz — agents need fresh pos for decide()
	constexpr float statusLogInterval    = 60.0f;

	// Client must send any message within this window or server drops + runs full
	// disconnect cleanup. Heartbeat cadence is 2s, so 15s ≈ 7 missed beats.
	constexpr float clientIdleTimeoutSec = 15.0f;

	constexpr float defaultDecideDuration  = 0.25f;
	constexpr int   maxDecidesPerTick      = 50;      // per-tick cap (50 Hz → 2500/s)
	constexpr float decisionSweepInterval  = 5.0f;
	constexpr float proximityRadius        = 16.0f;   // blocks
	constexpr float proximityCheckInterval = 0.5f;
}

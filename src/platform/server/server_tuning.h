#pragma once

/**
 * ServerTuning — centralized server configuration constants.
 *
 * All magic numbers for physics, networking, AI, and gameplay
 * in one place. Change values here to tune server behavior.
 */

namespace modcraft::ServerTuning {
	// Spawn
	constexpr float spawnHeightOffset    = 1.5f;  // blocks above surface to spawn entities

	// Stuck detection: check if entities tried to walk but didn't move
	constexpr float stuckCheckInterval   = 2.0f;  // seconds between stuck checks
	constexpr float stuckMinSpeed        = 0.3f;  // min velocity to count as "trying to walk"
	constexpr float stuckMaxDisplacement = 0.1f;  // if moved less than this, considered stuck
	constexpr float unstuckNudgeHeight   = 1.5f;  // teleport up by this many blocks to unstick
	constexpr float unstuckNudgeHoriz    = 0.5f;  // nudge horizontally to clear the block

	// Physics
	constexpr float gravity              = 32.0f; // ~3.2x real g, snappy Minecraft-like fall
	constexpr float entityStepHeight     = 1.0f;

	// Navigation (server-side greedy steering)
	constexpr float navArriveDistance    = 1.2f;   // XZ distance to consider arrived
	constexpr float navDodgeAngle        = 0.785f; // 45 degrees in radians
	constexpr float navDodgeDuration     = 1.5f;   // seconds to commit to a dodge
	constexpr float navStuckTimeout      = 2.0f;   // seconds without progress → dodge
	constexpr float navStuckMinMove      = 0.5f;   // blocks — less than this in stuckTimeout = stuck
	constexpr float navFormationSpacing  = 2.0f;   // blocks between entities in formation grid

	// Gameplay
	constexpr float hpRegenInterval      = 10.0f;  // seconds between +1 HP regen ticks
	constexpr float structureRegenCheckInterval = 5.0f;  // how often to scan the dirty-set for regen
	                                                       // actual regen rate is per-blueprint regen_interval_s

	// Network
	constexpr float tickRate             = 1.0f / 60.0f;  // 60 tps
	constexpr float broadcastInterval    = 0.05f;          // 20 Hz — agents need fresh position for behavior decisions
	constexpr float statusLogInterval    = 60.0f;          // seconds between status prints

	// Heartbeat / liveness. Client must send *something* (C_HEARTBEAT or any
	// other message) at least this often, or the server will drop it and run
	// the full disconnect cleanup (snapshot owned NPCs, save inventory, despawn).
	// Headroom: client sends C_HEARTBEAT every 2s, so 15s = ~7 missed beats.
	constexpr float clientIdleTimeoutSec = 15.0f;

	// Decision queue (agent-side scheduling, constants here for single source of truth)
	constexpr float defaultDecideDuration  = 0.25f;   // seconds — default when Python returns 2-tuple
	constexpr int   maxDecidesPerTick      = 50;       // per-tick budget cap (at 50 Hz → 2500 decide/s)
	constexpr float decisionSweepInterval  = 5.0f;     // seconds between orphan-detection sweeps
	constexpr float proximityRadius        = 16.0f;    // blocks: player client Creatures proximity detection
	constexpr float proximityCheckInterval = 0.5f;     // seconds: how often GUI client scans for nearby NPCs
}

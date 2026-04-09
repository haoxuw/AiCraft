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

	// Network
	constexpr float tickRate             = 1.0f / 60.0f;  // 60 tps
	constexpr float broadcastInterval    = 0.25f;          // 4 Hz — client predicts locally from moveTarget
	constexpr float statusLogInterval    = 5.0f;           // seconds between status prints
}

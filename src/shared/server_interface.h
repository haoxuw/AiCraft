#pragma once

/**
 * ServerInterface — abstract access to the game server.
 *
 * Game class uses this interface instead of GameServer directly.
 * Two implementations:
 *   - LocalServer: in-process GameServer (singleplayer / self-hosted)
 *   - NetworkServer: TCP client connecting to a remote server
 *
 * This lets the same Game class (with full menu, HUD, code editor)
 * work in both singleplayer and multiplayer. The user experience is
 * identical — only the backend changes.
 *
 * Flow:
 *   1. Client starts with menu (no server yet)
 *   2. User clicks "Play" → Game calls createGame()
 *      - Singleplayer: starts local GameServer in-process
 *      - Multiplayer: connects to remote server via TCP
 *   3. Game loop: sendAction() + tick() + read state for rendering
 *   4. User quits → Game calls disconnect()
 */

#include "shared/types.h"
#include "shared/chunk_source.h"
#include "shared/entity.h"
#include "shared/action.h"
#include "server/world_gen_config.h"
#include <string>
#include <functional>
#include <memory>

namespace agentica {

class ServerInterface {
public:
	virtual ~ServerInterface() = default;

	// --- Lifecycle ---

	// Create/join a game world. Returns true on success.
	virtual bool createGame(int seed, int templateIndex,
	                        const WorldGenConfig& wgc = WorldGenConfig{}) = 0;

	// Disconnect from game (stop local server or close TCP connection)
	virtual void disconnect() = 0;

	// Is a game currently active?
	virtual bool isConnected() const = 0;

	// --- Per-frame ---

	// Advance simulation (local: runs physics; network: polls for updates)
	virtual void tick(float dt) = 0;

	// Send an action proposal to the server
	virtual void sendAction(const ActionProposal& action) = 0;

	// Notify server that player reassigned a hotbar slot
	virtual void sendHotbarSlot(int slot, const std::string& itemId) {}

	// --- State access (for rendering) ---

	// Chunk data source (for terrain meshing)
	virtual ChunkSource& chunks() = 0;

	// Entity access
	virtual EntityId localPlayerId() const = 0;
	virtual Entity* getEntity(EntityId id) = 0;
	virtual void forEachEntity(std::function<void(Entity&)> fn) = 0;
	virtual size_t entityCount() const = 0;

	// Behavior access
	struct BehaviorInfo {
		std::string name;
		std::string sourceCode;
		std::string goal;
		bool hasError = false;
		std::string errorText;
	};
	virtual BehaviorInfo getBehaviorInfo(EntityId id) = 0;

	// World state
	virtual float worldTime() const = 0;
	virtual glm::vec3 spawnPos() const = 0;
	virtual float pickupRange() const = 0;

	// Block registry (for HUD, raycast)
	virtual const BlockRegistry& blockRegistry() const = 0;

	// Action queue (for client input → server)
	virtual ActionQueue& actionQueue() = 0;

	// Set client-side effect callbacks — invoked by NetworkServer when it receives
	// the corresponding server messages over TCP.
	virtual void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, const std::string&)> onBlockBreakText = nullptr,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr
	) = 0;
};

} // namespace agentica

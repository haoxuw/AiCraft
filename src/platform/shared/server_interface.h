#pragma once

/**
 * ServerInterface — abstract access to the game server.
 *
 * The only implementation is NetworkServer (TCP client). Singleplayer
 * spawns modcraft-server as a child process and connects via localhost TCP —
 * identical code path to multiplayer.
 *
 * Flow:
 *   1. Client starts with menu (no server yet)
 *   2. User clicks "Play" → Game spawns server process + connects via TCP
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

namespace modcraft {

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

	// Request an entity's inventory (chest, Creatures, etc.)
	virtual void sendGetInventory(EntityId eid) {}

	// Navigation goals — RPG/RTS click-to-move, server handles directly
	virtual void sendSetGoal(EntityId eid, glm::vec3 pos) {}
	virtual void sendSetGoalGroup(glm::vec3 pos, const std::vector<EntityId>& eids) {}
	virtual void sendCancelGoal(EntityId eid) {}

	// Proximity — notify server which NPCs are near the player (triggers AI re-decide)
	virtual void sendProximity(const std::vector<EntityId>& eids) {}

	// --- State access (for rendering) ---

	// Chunk data source (for terrain meshing)
	virtual ChunkSource& chunks() = 0;

	// Entity access
	virtual EntityId localPlayerId() const = 0;

	// The entity the local client is currently "driving" with input + camera.
	// Defaults to the local player, but may be any owned entity via Control mode.
	virtual EntityId controlledEntityId() const { return localPlayerId(); }
	virtual void setControlledEntityId(EntityId /*eid*/) {}

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

	// True once the server has finished initial setup for this client
	// (mobs spawned, welcome/inventory sent). Loading screen waits on this.
	// Default: assume ready (in-process TestServer etc).
	virtual bool isServerReady() const { return true; }

	// Server-reported prep progress in [0..1], pushed via S_PREPARING while
	// the server generates required chunks in the background. Returns -1
	// when no S_PREPARING has been seen yet (e.g. in-process TestServer
	// that skips the handshake entirely). Default: -1.
	virtual float preparingProgress() const { return -1.0f; }

	// Latest server-reported position for an entity (from broadcast).
	// Used by the debug HUD to show client-server divergence.
	// Default: same as the entity's current position (no divergence info).
	virtual glm::vec3 getServerPosition(EntityId id) {
		Entity* e = getEntity(id);
		return e ? e->position : glm::vec3(0);
	}

	// Block registry (for HUD, raycast)
	virtual const BlockRegistry& blockRegistry() const = 0;

	// Proposal queue (for client input → server)
	virtual ActionProposalQueue& proposalQueue() = 0;

	// Set client-side effect callbacks — invoked by NetworkServer when it receives
	// the corresponding server messages over TCP.
	virtual void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, const std::string&)> onBlockBreakText = nullptr,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr
	) = 0;

	// Fired whenever an S_INVENTORY snapshot is applied to a known entity.
	// The client uses this to repopulate UI-only views (e.g. the hotbar) when
	// the server pushes a fresh inventory. Default: no-op.
	virtual void setInventoryCallback(std::function<void(EntityId)> /*onInventoryUpdate*/) {}
};

} // namespace modcraft

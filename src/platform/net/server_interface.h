#pragma once

// Abstract server access. Only impl is NetworkServer (TCP) — singleplayer spawns
// civcraft-server as a child and localhost-TCPs; no in-process shortcut.

#include "logic/types.h"
#include "logic/chunk_source.h"
#include "logic/entity.h"
#include "logic/action.h"
#include "logic/annotation.h"
#include "server/world_gen_config.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <utility>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace civcraft {

class ServerInterface {
public:
	virtual ~ServerInterface() = default;

	// Lifecycle
	virtual bool createGame(int seed, int templateIndex,
	                        const WorldGenConfig& wgc = WorldGenConfig{}) = 0;
	virtual void disconnect() = 0;
	virtual bool isConnected() const = 0;

	// Non-blocking handshake: UI callers send HELLO then pollWelcome() every
	// frame, so the window keeps pumping events instead of freezing for the
	// seconds the server spends streaming prep chunks. Default falls back to
	// the blocking createGame() for interfaces that don't need it.
	virtual bool beginConnect(int seed = 42, int templateIndex = 1) {
		return createGame(seed, templateIndex);
	}
	virtual bool pollWelcome() { return isConnected(); }

	// Chosen playable creature type (sent in C_HELLO, so call BEFORE
	// createGame). Empty ⇒ server picks its default playable. No-op for
	// in-process server implementations that don't use the handshake.
	virtual void setCreatureType(const std::string& /*type*/) {}

	// Per-frame
	virtual void tick(float dt) = 0;
	virtual void sendAction(const ActionProposal& action) = 0;
	virtual void sendGetInventory(EntityId eid) {}

	// Click-to-move and RTS group commands run client-side now — behaviors emit
	// Move actions via civcraft_engine.Move / .Navigator; no server RPC.

	// Triggers NPC re-decide when listed eids enter player proximity.
	virtual void sendProximity(const std::vector<EntityId>& eids) {}

	// Rendering state access
	virtual ChunkSource& chunks() = 0;
	virtual EntityId localPlayerId() const = 0;
	// Durable ownership key; AgentClient filters adopted entities on this.
	// Legacy paths that haven't upgraded return SEAT_NONE (0), which the agent
	// treats as "adopt nothing yet."
	virtual uint32_t localSeatId() const { return 0; }

	// Controlled = whichever owned entity the local input drives. Defaults to localPlayer.
	virtual EntityId controlledEntityId() const { return localPlayerId(); }
	virtual void setControlledEntityId(EntityId /*eid*/) {}

	virtual Entity* getEntity(EntityId id) = 0;
	virtual void forEachEntity(std::function<void(Entity&)> fn) = 0;
	virtual size_t entityCount() const = 0;

	struct BehaviorInfo {
		std::string name;
		std::string sourceCode;
		std::string goal;
		bool hasError = false;
		std::string errorText;
	};
	virtual BehaviorInfo getBehaviorInfo(EntityId id) = 0;

	virtual float worldTime() const = 0;
	// Integer days elapsed since world creation. Season = (dayCount / kDaysPerSeason) % 4.
	virtual uint32_t dayCount() const { return 0; }
	virtual glm::vec3 spawnPos() const = 0;
	virtual float pickupRange() const = 0;

	// Server-authoritative weather (S_WEATHER). Defaults = "clear, still air" for headless tools.
	virtual const std::string& weatherKind() const {
		static const std::string kClear = "clear";
		return kClear;
	}
	virtual float weatherIntensity() const { return 0.0f; }
	virtual glm::vec2 weatherWind()   const { return {0.0f, 0.0f}; }
	virtual uint32_t weatherSeq()     const { return 0; }

	// After initial mob spawn + welcome/inventory sent. Loading screen waits on this.
	virtual bool isServerReady() const { return true; }

	// Last S_ERROR for surfacing on loading/menu screen. Empty = none.
	virtual const std::string& lastError() const {
		static const std::string empty;
		return empty;
	}

	// S_PREPARING progress [0..1] while server generates required chunks; -1 = unseen.
	virtual float preparingProgress() const { return -1.0f; }

	// Last broadcast position; debug HUD uses this for divergence display.
	virtual glm::vec3 getServerPosition(EntityId id) {
		Entity* e = getEntity(id);
		return e ? e->position : glm::vec3(0);
	}

	virtual const BlockRegistry& blockRegistry() const = 0;

	// Null if chunk uncached or has no annotations.
	virtual const std::vector<std::pair<glm::ivec3, Annotation>>*
	annotationsForChunk(ChunkPos /*cp*/) const { return nullptr; }

	virtual ActionProposalQueue& proposalQueue() = 0;

	// Invoked by NetworkServer when TCP messages arrive.
	virtual void setEffectCallbacks(
		std::function<void(ChunkPos)> onChunkDirty,
		std::function<void(glm::vec3, const std::string&)> onBlockBreakText = nullptr,
		std::function<void(glm::vec3, const std::string&)> onBlockPlace = nullptr
	) = 0;

	// Client-side prediction: apply a break locally (write AIR + fire the
	// usual break callbacks) without waiting for the server S_BLOCK. The
	// GUI client calls this right after sendAction(Convert) so the player
	// doesn't see the ~round-trip + remesh-pipeline lag. Test harnesses
	// that don't use a LocalWorld can leave the default no-op in place.
	virtual void predictBlockBreak(glm::ivec3 /*wpos*/) {}

	// Client-side prediction: apply a placement locally (write the given
	// block + fire the usual place callbacks). Mirrors predictBlockBreak.
	// Server rejects snap back via the re-emitted onBlockChange that
	// GameServer::resolveActions sends on Convert reject (see server.cpp).
	virtual void predictBlockPlace(glm::ivec3 /*wpos*/,
	                                BlockId /*bid*/,
	                                uint8_t /*param2*/,
	                                uint8_t /*appearance*/) {}

	// Fired on S_INVENTORY apply — client re-populates hotbar/UI views.
	virtual void setInventoryCallback(std::function<void(EntityId)> /*onInventoryUpdate*/) {}

	// Fired on S_REMOVE — client decides FX/SFX from the reason byte
	// (EntityRemovalReason wire value). `pos` is the entity's last known
	// position from the delta cache, so the puff lands where the entity was.
	// Tests and headless clients leave the default no-op.
	virtual void setEntityRemoveCallback(
		std::function<void(EntityId, glm::vec3 /*pos*/, uint8_t /*reason*/)> /*cb*/) {}
};

} // namespace civcraft

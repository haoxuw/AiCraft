#pragma once

/**
 * GameServer — authoritative world simulation.
 *
 * Owns the World, runs the 3-phase game loop, validates all actions.
 * Clients connect and send ActionProposals; server validates, executes,
 * and broadcasts state updates back.
 *
 * Can run:
 *   - In-process (singleplayer): Game creates server + connects locally
 *   - Standalone (dedicated): main_server.cpp runs headless
 */

#include "server/world.h"
#include "server/entity_manager.h"
#include "server/world_gen_config.h"
#include "shared/action.h"
#include "shared/constants.h"
#include "server/world_template.h"
#include "server/pathfind.h"
#include "shared/block_registry.h"
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <cstdio>

namespace modcraft {

using ClientId = uint32_t;

// Rejection codes logged when server rejects an action proposal.
// Stored as uint32_t in log output for fast filtering without string allocation.
enum class ActionRejectCode : uint32_t {
	ValueConservationViolated = 1,
	ItemNotInInventory        = 2,
	SourceEntityGone          = 3,
	SourceBlockGone           = 4,
	SourceBlockTypeMismatch   = 5,
	PlacementTargetOccupied   = 6,
	UnknownBlockType          = 7,
	ChunkNotLoaded            = 8,
	PickupOutOfRange          = 9,
};

// Server-side callbacks — used by dedicated server (main_server.cpp) to broadcast
// world state changes to all connected clients over TCP.
struct ServerCallbacks {
	std::function<void(glm::ivec3 pos, BlockId oldBid, BlockId newBid, uint8_t p2)> onBlockChange; // block placed/broken
	std::function<void(EntityId id)> onEntityRemove;                     // entity despawned
	std::function<void(EntityId id, const Inventory&)> onInventoryChange; // inventory updated
};

struct ServerConfig {
	int seed = 42;
	int templateIndex = 1;  // VillageWorld (has trees)
	int port = 7777;
	WorldGenConfig worldGenConfig;
};

// All server tuning constants in one header
#include "server/server_tuning.h"

class GameServer {
public:
	// Initialize world only (no entity spawning) — used by loadWorld
	void initWorld(const ServerConfig& config,
	               const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		auto tmpl = (config.templateIndex < (int)templates.size())
			? templates[config.templateIndex] : templates[0];

		m_world = std::make_unique<World>(config.seed, tmpl, config.templateIndex);
		m_wgc = config.worldGenConfig;
		m_worldTime = 0.25f; // start at dawn

		// Ask the template where the player should spawn
		glm::vec3 rawSpawn = tmpl->preferredSpawn(config.seed);
		float sx = rawSpawn.x, sz = rawSpawn.z;

		// Safety scan upward to escape any structure or tree placed at spawn
		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& def = m_world->blocks.get(m_world->getBlock(x, y, z));
			return def.solid ? def.collision_height : 0.0f;
		};
		int spawnY = (int)std::round(rawSpawn.y);
		for (int scan = 0; scan < 24; scan++) {
			bool clear = solidFn((int)sx, spawnY,     (int)sz) <= 0.0f &&
			             solidFn((int)sx, spawnY + 1, (int)sz) <= 0.0f &&
			             solidFn((int)sx, spawnY + 2, (int)sz) <= 0.0f;
			if (clear) break;
			spawnY++;
		}
		m_spawnPos = {sx, (float)spawnY, sz};
	}

	// Initialize server with world + spawn default entities (new world)
	void init(const ServerConfig& config,
	          const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		initWorld(config, templates);

		auto& tmpl = m_world->getTemplate();
		auto& wgc  = config.worldGenConfig;

		// Scan upward from terrain noise height to find the actual topmost solid block,
		// so mobs don't spawn inside village buildings placed on top of terrain.
		auto actualSurfaceY = [&](float x, float z) -> float {
			float terrainY = m_world->surfaceHeight(x, z);
			int startY = std::max(0, (int)std::round(terrainY) - 1);
			int topSolid = startY - 1;
			int bx = (int)std::round(x), bz = (int)std::round(z);
			for (int y = startY; y <= startY + 30; y++) {
				BlockId bid = m_world->getBlock(bx, y, bz);
				if (m_world->blocks.get(bid).solid)
					topSolid = y;
				else if (y > topSolid + 5)
					break;
			}
			return (float)(topSolid + 1);
		};
		auto safeSpawnHeight = [&](float x, float z) {
			return actualSurfaceY(x, z) + ServerTuning::spawnHeightOffset;
		};

		// Village center from template (virtual — works for any template type)
		auto vc = tmpl.villageCenter(m_world->seed());
		float mobCX = (float)vc.x, mobCZ = (float)vc.y;

		// Build mob list: prefer per-mob radius from template's Python config,
		// fall back to wgc.mobs (which may have come from WorldGenConfig defaults or UI).
		// If wgc.mobs is empty, use the template's Python config mobs.
		std::vector<MobSpawn> mobList = wgc.mobs;
		if (mobList.empty()) {
			for (auto& mc : tmpl.pyConfig().mobs)
				mobList.push_back({mc.type, mc.count, mc.radius});
		}

		auto spawnMob = [&](const std::string& typeId, int count, float radius, float baseOffset) {
			for (int m = 0; m < count; m++) {
				float angle = (float)m / (float)count * 6.28318f + baseOffset;
				float emx = mobCX + std::cos(angle) * radius;
				float emz = mobCZ + std::sin(angle) * radius;

				std::unordered_map<std::string, PropValue> extraProps;
				auto bIt = wgc.behaviorOverrides.find(typeId);
				if (bIt != wgc.behaviorOverrides.end())
					extraProps[Prop::BehaviorId] = bIt->second;

				EntityId eid = m_world->entities.spawn(typeId,
					{emx, safeSpawnHeight(emx, emz), emz}, extraProps);

				auto iIt = wgc.startingItems.find(typeId);
				if (iIt != wgc.startingItems.end()) {
					Entity* e = m_world->entities.get(eid);
					if (e && e->inventory) {
						for (auto& [itemId, cnt] : iIt->second)
							e->inventory->add(itemId, cnt);
					}
				}
			}
		};

		// Place chests in all non-barn houses. Returns positions in house order.
		auto houseChests = tmpl.houseChestPositions(m_world->seed());
		BlockId chestId = m_world->blocks.getId(BlockType::Chest);
		m_houseChests.clear();
		for (auto& cPos : houseChests) {
			int cx = (int)std::round(cPos.x), cy = (int)std::round(cPos.y), cz = (int)std::round(cPos.z);
			if (chestId != BLOCK_AIR) {
				ChunkPos cp = worldToChunk(cx, cy, cz);
				Chunk* c = m_world->getChunk(cp);
				if (c) c->set(((cx%16)+16)%16, ((cy%16)+16)%16, ((cz%16)+16)%16, chestId);
			}
			glm::vec3 blockCenter = {(float)cx + 0.5f, (float)cy + 0.5f, (float)cz + 0.5f};
			m_houseChests.push_back(blockCenter);

			// Register block inventory for this chest (keyed by block position)
			uint64_t posKey = packBlockPos(cx, cy, cz);
			m_blockInventories[posKey]; // default-construct empty inventory
			printf("[Server] Chest at (%d,%d,%d)\n", cx, cy, cz);
		}
		m_chestPos = m_houseChests.empty() ? m_spawnPos : m_houseChests[0];

		// Spawn bed-assigned villagers: one per bed, home_x/home_z + chest_x/y/z set at spawn.
		// This replaces the generic villager entry in mobList.
		auto beds = tmpl.bedPositions(m_world->seed());
		if (!beds.empty()) {
			const std::string villagerType = "base:villager";
			for (size_t i = 0; i < beds.size(); i++) {
				const auto& bp = beds[i];
				std::unordered_map<std::string, PropValue> extraProps;
				auto bIt = wgc.behaviorOverrides.find(villagerType);
				if (bIt != wgc.behaviorOverrides.end())
					extraProps[Prop::BehaviorId] = bIt->second;
				float homeY = safeSpawnHeight(bp.x, bp.z);
				extraProps["home_x"] = bp.x;
				extraProps["home_y"] = homeY;
				extraProps["home_z"] = bp.z;
				if (i < m_houseChests.size()) {
					extraProps["chest_x"] = m_houseChests[i].x;
					extraProps["chest_y"] = m_houseChests[i].y;
					extraProps["chest_z"] = m_houseChests[i].z;
				}
				m_world->entities.spawn(villagerType,
					{bp.x, safeSpawnHeight(bp.x, bp.z), bp.z}, extraProps);
			}
			// Remove villagers from general mob list to avoid double-spawning
			mobList.erase(std::remove_if(mobList.begin(), mobList.end(),
				[&](const MobSpawn& ms) { return ms.typeId == villagerType; }),
				mobList.end());
		}

		// Spawn cats and dogs inside the barn (home_x/home_z set to barn center).
		// If no barn in this template, they fall through to the regular mob spawn.
		auto barnCtr = tmpl.barnCenter(m_world->seed());
		if (barnCtr.x >= 0) {
			const std::vector<std::string> barnAnimals = {"base:cat", "base:dog"};
			for (const auto& animalType : barnAnimals) {
				// Find this mob in the list
				for (auto& ms : mobList) {
					if (ms.typeId != animalType) continue;
					for (int m = 0; m < ms.count; m++) {
						// Spread randomly inside the barn interior (±5 blocks from center)
						float emx = (float)barnCtr.x + ((m % 3) - 1) * 4.0f;
						float emz = (float)barnCtr.y + ((m / 3) - 1) * 4.0f;
						std::unordered_map<std::string, PropValue> extraProps;
						auto bIt = wgc.behaviorOverrides.find(animalType);
						if (bIt != wgc.behaviorOverrides.end())
							extraProps[Prop::BehaviorId] = bIt->second;
						extraProps["home_x"] = (float)barnCtr.x;
						extraProps["home_z"] = (float)barnCtr.y;
						m_world->entities.spawn(animalType,
							{emx, safeSpawnHeight(emx, emz), emz}, extraProps);
					}
					ms.count = 0;  // mark as spawned so spawnMob loop skips it
				}
			}
		}

		for (int i = 0; i < (int)mobList.size(); i++) {
			if (mobList[i].count <= 0) continue;  // already spawned (barn animals)
			float r = (mobList[i].radius > 0) ? mobList[i].radius : wgc.mobSpawnRadius;
			spawnMob(mobList[i].typeId, mobList[i].count, r, (float)i);
		}

		printf("[Server] Initialized. Spawn: %.0f, %.0f, %.0f  Chests: %zu houses\n",
		       m_spawnPos.x, m_spawnPos.y, m_spawnPos.z, m_houseChests.size());
	}

	// Add a client. Returns the player's EntityId.
	EntityId addClient(ClientId clientId, const std::string& characterSkin = "") {
		// Always spawn as base:player (has inventory, HP, physics).
		// The characterSkin determines visual model, not entity type.
		EntityId eid = m_world->entities.spawn(EntityType::Player, m_spawnPos);
		Entity* pe = m_world->entities.get(eid);
		if (pe) pe->yaw = -90.0f; // face -Z (toward stairs from portal)
		// Server handles click-to-move navigation directly (no agent needed)
		// Self-owned: player controls their own character
		if (pe) pe->setProp(Prop::Owner, (int)eid);
		// Store character skin as entity property (client reads for rendering)
		if (pe && !characterSkin.empty())
			pe->setProp("character_skin", characterSkin);
		if (pe && pe->inventory) {
			// Try loading saved inventory for this character
			std::string skin = characterSkin.empty() ? "default" : characterSkin;
			auto savedIt = m_savedInventories.find(skin);
			if (savedIt != m_savedInventories.end()) {
				*pe->inventory = savedIt->second;
				printf("[Server] Restored saved inventory for '%s'\n", skin.c_str());
			} else {
				// First time — give starting items
				auto sit = m_wgc.startingItems.find(EntityType::Player);
				if (sit != m_wgc.startingItems.end()) {
					for (auto& [item, count] : sit->second)
						pe->inventory->add(item, count);
				} else {
					pe->inventory->add(BlockType::Stone, 10);
					pe->inventory->add(BlockType::Wood, 10);
					pe->inventory->add("base:sword", 1);
					pe->inventory->add("base:shield", 1);
					pe->inventory->add("base:potion", 3);
				}
				pe->inventory->autoPopulateHotbar();
			}
		}
		m_clients[clientId] = {eid, false, {}};
		printf("[Server] Client %u joined. Player entity: %u\n", clientId, eid);
		return eid;
	}

	// Add an agent client (no player entity — controls existing NPC entities).
	void addAgentClient(ClientId clientId) {
		m_clients[clientId] = {ENTITY_NONE, true};
		printf("[Server] Agent client %u joined.\n", clientId);
	}

	// Assign an entity to a bot client for AI control.
	bool assignEntityToClient(ClientId clientId, EntityId entityId) {
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) return false;
		it->second.controlledEntities.insert(entityId);
		m_entityOwner[entityId] = clientId;
		printf("[Server] Entity %u assigned to client %u\n", entityId, clientId);
		return true;
	}

	// Revoke entity control from a client.
	void revokeEntityFromClient(ClientId clientId, EntityId entityId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end())
			it->second.controlledEntities.erase(entityId);
		m_entityOwner.erase(entityId);
	}

	// Get entities controlled by a client.
	std::vector<EntityId> getControlledEntities(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) return {};
		return std::vector<EntityId>(it->second.controlledEntities.begin(),
		                             it->second.controlledEntities.end());
	}

	// Get NPC entities that have a BehaviorId but no agent client.
	// Player entities are excluded — server handles their navigation directly.
	std::vector<EntityId> getUncontrolledNPCs() const {
		std::vector<EntityId> result;
		m_world->entities.forEach([&](Entity& e) {
			if (e.removed) return;
			std::string bid = e.getProp<std::string>(Prop::BehaviorId, "");
			if (bid.empty()) return;
			// Skip playable entities — server handles their navigation via C_SET_GOAL.
			if (e.def().playable) return;
			auto ownerIt = m_entityOwner.find(e.id());
			if (ownerIt == m_entityOwner.end()) {
				result.push_back(e.id());
			}
		});
		return result;
	}

	// Check if an entity is controlled by any client.
	bool isEntityControlled(EntityId id) const {
		return m_entityOwner.find(id) != m_entityOwner.end();
	}

	// Check if a client is allowed to control an entity.
	// Rules: (1) agent clients can always control their assigned entities,
	//        (2) GUI clients can control entities they own (Prop::Owner matches their player),
	//        (3) admin clients (player has fly_mode) can control any entity.
	bool canClientControl(ClientId clientId, EntityId targetId) const {
		auto cit = m_clients.find(clientId);
		if (cit == m_clients.end()) return false;
		// Agent clients: always allowed (they're assigned by the server)
		if (cit->second.isAgent) return true;
		// GUI client: check admin (fly_mode on their player entity)
		Entity* playerEnt = m_world->entities.get(cit->second.playerEntityId);
		if (playerEnt && playerEnt->getProp<bool>("fly_mode", false))
			return true; // admin — control anything
		// Normal: target must be owned by this client's player
		Entity* target = m_world->entities.get(targetId);
		if (!target) return false;
		int ownerId = target->getProp<int>(Prop::Owner, 0);
		return ownerId == (int)cit->second.playerEntityId;
	}

	// Get the client that controls an entity (ENTITY_NONE if uncontrolled).
	ClientId getEntityOwner(EntityId id) const {
		auto it = m_entityOwner.find(id);
		return it != m_entityOwner.end() ? it->second : 0;
	}

	void removeClient(ClientId clientId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end()) {
			// Save player inventory before removing
			if (it->second.playerEntityId != ENTITY_NONE) {
				Entity* pe = m_world->entities.get(it->second.playerEntityId);
				if (pe && pe->inventory) {
					std::string skin = pe->getProp<std::string>("character_skin", "default");
					m_savedInventories[skin] = *pe->inventory;
					printf("[Server] Saved inventory for '%s'\n", skin.c_str());
				}
				m_world->entities.remove(it->second.playerEntityId);
			}
			// Release all controlled entities
			for (EntityId eid : it->second.controlledEntities)
				m_entityOwner.erase(eid);
			m_clients.erase(it);
			printf("[Server] Client %u disconnected.\n", clientId);
		}
	}

	// Receive an action from a client
	// Submit action directly without ownership check (test-only / agent-internal use).
	void receiveActionDirect(const ActionProposal& action) {
		if (!action.behaviorSource.empty()) {
			m_pendingReloads.push_back(action);
			return;
		}
		m_world->proposals.propose(action);
	}

	void receiveAction(ClientId clientId, ActionProposal action) {
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) return;

		// Ownership check: client must own the entity or be admin.
		// Agent clients are always allowed (server-assigned). GUI clients
		// must own the target entity (Prop::Owner == their player) or be
		// in admin mode (fly_mode). This prevents griefing in multiplayer
		// while allowing full RTS control of owned entities.
		if (!canClientControl(clientId, action.actorId)) return;

		// Agent goal text: update server-side entity for broadcast (all action types)
		if (!action.goalText.empty()) {
			Entity* e = m_world->entities.get(action.actorId);
			if (e) e->goalText = action.goalText;
		}

		// behaviorSource non-empty = hot-reload control message, not a game action.
		// Store separately for the network layer to forward to agent clients.
		if (!action.behaviorSource.empty()) {
			m_pendingReloads.push_back(action);
			return;
		}

		m_world->proposals.propose(action);
	}

	// Get and clear pending behavior reload requests.
	std::vector<ActionProposal> drainPendingReloads() {
		auto result = std::move(m_pendingReloads);
		m_pendingReloads.clear();
		return result;
	}

	// Set callbacks for visual effects (client provides these)
	void setCallbacks(ServerCallbacks cb) { m_callbacks = cb; }
	ServerCallbacks& callbacks() { return m_callbacks; }

	// Main server tick
	void tick(float dt) {
		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& def = m_world->blocks.get(m_world->getBlock(x, y, z));
			return def.solid ? def.collision_height : 0.0f;
		};

		// AI behavior decisions arrive as ActionProposals from bot client
		// processes — no server-side AI gathering needed.

		// Phase 1: Resolve all proposals (may set entity.removed = true)
		resolveActions(dt);

		// Broadcast entity removals BEFORE stepPhysics erases them from the map
		if (m_callbacks.onEntityRemove) {
			m_world->entities.forEachIncludingRemoved([&](Entity& e) {
				if (e.removed && !e.removalBroadcast) {
					m_callbacks.onEntityRemove(e.id());
					e.removalBroadcast = true;
				}
			});
		}

		// Server-side navigation: set velocities for entities with active nav goals
		updateNavigation(dt, m_world->entities);

		// Physics for all entities (purges removed entities from the map)
		m_world->entities.stepPhysics(dt, solidFn);

		// Active block ticking (TNT, wheat, wire)
		m_activeBlockTimer += dt;
		if (m_activeBlockTimer >= 0.05f) {
			m_world->tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId bid) {
				glm::ivec3 pos{bx, by, bz};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pos, BLOCK_AIR, bid, 0);
			});
			m_activeBlockTimer = 0;
		}

		// Item pickup is CLIENT-INITIATED: clients send PickupItem actions.
		// Server validates and executes in resolveActions().
		// NPC pickup is handled by Python behavior → PickupItem action.

		// Advance world time
		m_worldTime += (1.0f / 1200.0f) * dt; // 20-min cycle: 5min each night/morning/afternoon/evening

		// Stuck detection: periodically check if walking entities haven't moved
		m_stuckTimer += dt;
		if (m_stuckTimer >= ServerTuning::stuckCheckInterval) {
			m_stuckTimer = 0;

			m_world->entities.forEach([&](Entity& e) {
				// Check all living entities (anything with HP)
				if (!e.def().isLiving()) return;
				// Skip entities with active nav — nav has its own stuck handling
				if (e.nav.active) return;

				EntityId id = e.id();
				auto it = m_lastPositions.find(id);
				if (it == m_lastPositions.end()) {
					// First check — record position
					m_lastPositions[id] = e.position;
					return;
				}

				// Was the entity trying to walk?
				float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
				if (hSpeed < ServerTuning::stuckMinSpeed) {
					it->second = e.position;
					return; // not trying to move, not stuck
				}

				// Did it actually move?
				float displacement = glm::length(glm::vec2(
					e.position.x - it->second.x, e.position.z - it->second.z));

				if (displacement < ServerTuning::stuckMaxDisplacement) {
					// Stuck! Nudge up and slightly sideways to clear the obstacle
					float nudgeX = (e.velocity.x > 0 ? 1 : -1) * ServerTuning::unstuckNudgeHoriz;
					float nudgeZ = (e.velocity.z > 0 ? 1 : -1) * ServerTuning::unstuckNudgeHoriz;
					e.position.y += ServerTuning::unstuckNudgeHeight;
					e.position.x += nudgeX;
					e.position.z += nudgeZ;
					e.velocity.y = 0; // let gravity handle the drop
				}

				it->second = e.position;
			});

			// Clean up entries for removed entities
			for (auto it = m_lastPositions.begin(); it != m_lastPositions.end(); ) {
				if (!m_world->entities.get(it->first))
					it = m_lastPositions.erase(it);
				else
					++it;
			}
		}
	}

	// Accessors
	World& world() { return *m_world; }
	const World& world() const { return *m_world; }
	float worldTime() const { return m_worldTime; }
	void setWorldTime(float t) { m_worldTime = t; }
	glm::vec3 spawnPos() const { return m_spawnPos; }
	void setSpawnPos(glm::vec3 p) { m_spawnPos = p; }
	const WorldGenConfig& worldGenConfig() const { return m_wgc; }

	// Per-character inventory persistence
	std::unordered_map<std::string, Inventory>& savedInventories() { return m_savedInventories; }
	const std::unordered_map<std::string, Inventory>& savedInventories() const { return m_savedInventories; }

	// Block inventories (chests) — keyed by packed block position
	const std::unordered_map<uint64_t, Inventory>& blockInventories() const { return m_blockInventories; }

	// Pack block coords into a key for blockInventories
	static uint64_t packBlockPos(int x, int y, int z) {
		return ((uint64_t)(uint32_t)x)
		     | ((uint64_t)(uint32_t)y << 21)
		     | ((uint64_t)(uint32_t)z << 42);
	}
	EntityId getPlayerEntity(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		return it != m_clients.end() ? it->second.playerEntityId : ENTITY_NONE;
	}

private:
	void resolveActions(float dt);

	std::unique_ptr<World> m_world;
	ServerCallbacks m_callbacks;
	WorldGenConfig m_wgc;
	float m_worldTime = 0.30f;
	float m_activeBlockTimer = 0;
	float m_stuckTimer = 0;
	glm::vec3 m_spawnPos = {30, 10, 30};
	glm::vec3 m_chestPos = {30, 10, 30};
	std::vector<glm::vec3> m_houseChests;                           // one per non-barn house
	std::unordered_map<uint64_t, Inventory> m_blockInventories;     // packed(x,y,z) → inventory
	std::unordered_map<std::string, Inventory> m_savedInventories;  // character_skin → inventory

	// Stuck detection: last known position per entity (checked every stuckCheckInterval)
	std::unordered_map<EntityId, glm::vec3> m_lastPositions;

	struct ClientState {
		EntityId playerEntityId = ENTITY_NONE;
		bool isAgent = false;
		std::unordered_set<EntityId> controlledEntities;
	};
	std::unordered_map<ClientId, ClientState> m_clients;
	std::unordered_map<EntityId, ClientId> m_entityOwner; // entity → controlling client
	std::vector<ActionProposal> m_pendingReloads; // behavior reload requests to forward to bots
};

} // namespace modcraft

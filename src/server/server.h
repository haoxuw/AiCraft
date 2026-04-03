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
#include "shared/block_registry.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <cstdio>

namespace agentworld {

using ClientId = uint32_t;

// Server-side effect callbacks (client provides these for visual feedback)
struct ServerCallbacks {
	std::function<void(ChunkPos cp)> onChunkDirty;
	std::function<void(glm::vec3 pos, glm::vec3 color, int count)> onBlockBreak;
	std::function<void(glm::vec3 pos, glm::vec3 color)> onItemPickup;
	std::function<void(glm::vec3 pos, const std::string& soundPlace)> onBlockPlace;
	// Network broadcast callbacks (set by main_server.cpp for dedicated server)
	std::function<void(glm::ivec3 pos, BlockId bid)> onBlockChange;     // block placed/broken
	std::function<void(EntityId id)> onEntityRemove;                     // entity despawned
	std::function<void(EntityId id, const Inventory&)> onInventoryChange; // inventory updated
	// Floating text triggers (client-only HUD effects)
	std::function<void(glm::vec3 pos, const std::string& name, int count)> onPickupText;
	std::function<void(glm::vec3 pos, const std::string& blockName)> onBreakText;
};

struct ServerConfig {
	int seed = 42;
	int templateIndex = 1;  // VillageWorld (has trees)
	bool creative = false;  // survival by default
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

		// Pass world gen config to template
		auto* village = dynamic_cast<VillageWorldTemplate*>(tmpl.get());
		if (village) village->setConfig(config.worldGenConfig);

		m_world = std::make_unique<World>(config.seed, tmpl, config.templateIndex);
		m_creative = config.creative;
		m_wgc = config.worldGenConfig;
		m_worldTime = 0.30f;

		// Ask the template where the player should spawn
		glm::vec3 rawSpawn = tmpl->preferredSpawn(config.seed);
		float sx = rawSpawn.x, sz = rawSpawn.z;

		// Safety scan upward to escape any structure or tree placed at spawn
		BlockSolidFn solidFn = [&](int x, int y, int z) {
			return m_world->blocks.get(m_world->getBlock(x, y, z)).solid;
		};
		int spawnY = (int)std::round(rawSpawn.y);
		for (int scan = 0; scan < 24; scan++) {
			bool clear = !solidFn((int)sx, spawnY,     (int)sz) &&
			             !solidFn((int)sx, spawnY + 1, (int)sz) &&
			             !solidFn((int)sx, spawnY + 2, (int)sz);
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

		auto safeSpawnHeight = [&](float x, float z) {
			return m_world->surfaceHeight(x, z) + ServerTuning::spawnHeightOffset;
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

		for (int i = 0; i < (int)mobList.size(); i++) {
			float r = (mobList[i].radius > 0) ? mobList[i].radius : wgc.mobSpawnRadius;
			spawnMob(mobList[i].typeId, mobList[i].count, r, (float)i);
		}

		// Place starter chest — template decides where (village: inside main house)
		glm::vec3 cPos = tmpl.chestPosition(m_world->seed(), m_spawnPos);
		int chestX = (int)std::round(cPos.x), chestZ = (int)std::round(cPos.z);
		int chestY = (int)std::round(cPos.y);

		BlockId chestId = m_world->blocks.getId(BlockType::Chest);
		if (chestId != BLOCK_AIR) {
			ChunkPos cp = worldToChunk(chestX, chestY, chestZ);
			Chunk* c = m_world->getChunk(cp);
			if (c) c->set(((chestX%16)+16)%16, ((chestY%16)+16)%16, ((chestZ%16)+16)%16, chestId);
		}
		m_chestPos = glm::vec3((float)chestX + 0.5f, (float)chestY, (float)chestZ + 0.5f);

		printf("[Server] Initialized. Spawn: %.0f, %.0f, %.0f (chest at %.0f,%.0f,%.0f)\n",
		       m_spawnPos.x, m_spawnPos.y, m_spawnPos.z,
		       m_chestPos.x, m_chestPos.y, m_chestPos.z);
	}

	// Add a client. Returns the player's EntityId.
	EntityId addClient(ClientId clientId, const std::string& characterSkin = "") {
		// Always spawn as base:player (has inventory, HP, physics).
		// The characterSkin determines visual model, not entity type.
		EntityId eid = m_world->entities.spawn(EntityType::Player, m_spawnPos);
		Entity* pe = m_world->entities.get(eid);
		// Store character skin as entity property (client reads for rendering)
		if (pe && !characterSkin.empty())
			pe->setProp("character_skin", characterSkin);
		if (pe && pe->inventory) {
			// Starting items: from config if specified, else defaults
			auto sit = m_wgc.startingItems.find(EntityType::Player);
			if (sit != m_wgc.startingItems.end()) {
				for (auto& [item, count] : sit->second)
					pe->inventory->add(item, count);
			} else if (m_creative) {
				for (auto* bt : {BlockType::Stone, BlockType::Dirt, BlockType::Grass,
				                 BlockType::Sand, BlockType::Wood, BlockType::Leaves,
				                 BlockType::Snow, BlockType::TNT, BlockType::Cobblestone})
					pe->inventory->add(bt, 999);
			} else {
				pe->inventory->add(BlockType::Stone, 10);
				pe->inventory->add(BlockType::Wood, 10);
				pe->inventory->add("base:sword", 1);
				pe->inventory->add("base:shield", 1);
				pe->inventory->add("base:potion", 3);
			}
			pe->inventory->autoPopulateHotbar();
		}
		m_clients[clientId] = {eid};
		printf("[Server] Client %u joined. Player entity: %u\n", clientId, eid);
		return eid;
	}

	void removeClient(ClientId clientId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end()) {
			m_world->entities.remove(it->second.playerEntityId);
			m_clients.erase(it);
			printf("[Server] Client %u disconnected.\n", clientId);
		}
	}

	// Receive an action from a client
	void receiveAction(ClientId clientId, ActionProposal action) {
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) return;

		// Anti-cheat: block/item actions must come from the client's own player.
		// Move actions are allowed for any entity (RTS commanding).
		if (action.type != ActionProposal::Move &&
		    action.actorId != it->second.playerEntityId)
			return;

		m_world->actions.propose(action);
	}

	// Set callbacks for visual effects (client provides these)
	void setCallbacks(ServerCallbacks cb) { m_callbacks = cb; }
	ServerCallbacks& callbacks() { return m_callbacks; }

	// Main server tick
	void tick(float dt) {
		BlockSolidFn solidFn = [&](int x, int y, int z) {
			return m_world->blocks.get(m_world->getBlock(x, y, z)).solid;
		};

		// Phase 3: AI behaviors gather decisions
		// Pass block query so behaviors can find trees, resources, etc.
		EntityManager::BlockTypeFn blockQuery = [&](int x, int y, int z) -> std::string {
			BlockId bid = m_world->getBlock(x, y, z);
			return m_world->blocks.get(bid).string_id;
		};
		m_world->entities.gatherDecisions(dt, m_world->actions, blockQuery);

		// Phase 1: Resolve all proposals
		resolveActions(dt);

		// Physics for all entities
		m_world->entities.stepPhysics(dt, solidFn);

		// Active block ticking (TNT, wheat, wire)
		m_activeBlockTimer += dt;
		if (m_activeBlockTimer >= 0.05f) {
			m_world->tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId bid) {
				if (m_callbacks.onChunkDirty)
					m_callbacks.onChunkDirty(World::worldToChunk(bx, by, bz));
				if (m_callbacks.onBlockBreak)
					m_callbacks.onBlockBreak(glm::vec3(bx, by, bz), {0.9f, 0.7f, 0.2f}, 4);
			});
			m_activeBlockTimer = 0;
		}

		// Broadcast entity removals BEFORE physics purges them
		if (m_callbacks.onEntityRemove) {
			m_world->entities.forEach([&](Entity& e) {
				// This iterates non-removed only, but items about to be removed
				// have item->removed = true already from pickup below or combat
			});
			// Check for removed entities directly
			m_world->entities.forEachIncludingRemoved([&](Entity& e) {
				if (e.removed && !e.removalBroadcast) {
					m_callbacks.onEntityRemove(e.id());
					e.removalBroadcast = true;
				}
			});
		}

		// Item pickup — only player-controlled entities attract items.
		// Animals have inventory (for loot/trading) but don't auto-pickup.
		m_world->entities.forEach([&](Entity& e) {
			if (!e.inventory) return;
			if (e.typeId() != EntityType::Player) return;
			glm::vec3 center = e.position;
			auto pickups = m_world->entities.attractItemsToward(center, 2.5f, 1.5f, dt);
			bool inventoryChanged = false;
			for (auto* item : pickups) {
				std::string itemType = item->getProp<std::string>(Prop::ItemType);
				int count = item->getProp<int>(Prop::Count, 1);
				e.inventory->add(itemType, count);
				if (m_callbacks.onItemPickup)
					m_callbacks.onItemPickup(item->position, {0.8f, 0.9f, 1.0f});
				if (m_callbacks.onPickupText)
					m_callbacks.onPickupText(item->position, itemType, count);
				item->removed = true;
				inventoryChanged = true;
			}
			if (inventoryChanged) {
				e.inventory->autoPopulateHotbar();
				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(e.id(), *e.inventory);
			}
		});

		// Advance world time
		m_worldTime += (1.0f / 600.0f) * dt;

		// Stuck detection: periodically check if walking entities haven't moved
		m_stuckTimer += dt;
		if (m_stuckTimer >= ServerTuning::stuckCheckInterval) {
			m_stuckTimer = 0;

			m_world->entities.forEach([&](Entity& e) {
				// Check all living entities (anything with HP)
				if (!e.def().isLiving()) return;

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
	bool isCreative() const { return m_creative; }

	EntityId getPlayerEntity(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		return it != m_clients.end() ? it->second.playerEntityId : ENTITY_NONE;
	}

private:
	void resolveActions(float dt);

	std::unique_ptr<World> m_world;
	ServerCallbacks m_callbacks;
	WorldGenConfig m_wgc;
	bool m_creative = true;
	float m_worldTime = 0.30f;
	float m_activeBlockTimer = 0;
	float m_stuckTimer = 0;
	glm::vec3 m_spawnPos = {30, 10, 30};
	glm::vec3 m_chestPos = {30, 10, 30};

	// Stuck detection: last known position per entity (checked every stuckCheckInterval)
	std::unordered_map<EntityId, glm::vec3> m_lastPositions;

	struct ClientState {
		EntityId playerEntityId;
	};
	std::unordered_map<ClientId, ClientState> m_clients;
};

} // namespace agentworld

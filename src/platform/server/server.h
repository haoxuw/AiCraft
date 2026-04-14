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
#include "shared/physics.h"
#include "server/world_template.h"
#include "server/pathfind.h"
#include "server/structure_blueprint.h"
#include "server/structure_block_cacher.h"
#include "shared/block_registry.h"
// Owned-NPC persistence across client sessions. Included at namespace scope
// (before the `namespace civcraft {` below) because its <string>/<vector>
// system includes must not land inside the civcraft namespace.
#include "server/owned_entity_store.h"
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <cstdio>
#include <chrono>
#include <mutex>

namespace civcraft {

// ClientId moved to shared/types.h so services like ChunkGenService can
// reference it without pulling the server header.

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
	TargetEntityGone          = 10,
	TargetHasNoInventory      = 11,
	ActorHasNoInventory       = 12,
	MissingItemId             = 13,
};

// Count-throttled action-reject log. Prints the FIRST 5 hits immediately so
// a fresh desync is visible the moment it starts, then every 100th hit to
// show sustained loops without flooding. Singleplayer localhost should
// never see any of these at all — every occurrence is a bug worth fixing.
// Emits to stderr with "[!!]" so it stands out in mixed stdout/stderr logs.
inline void logActionReject(const char* kind, EntityId eid,
                            const char* reason, const char* detail) {
	static std::unordered_map<uint64_t, uint32_t> s_count;
	static std::mutex s_mu;
	uint64_t key = ((uint64_t)eid << 32)
	             ^ (uint64_t)std::hash<std::string>{}(std::string(kind) + ":" + reason);
	uint32_t c;
	{
		std::lock_guard<std::mutex> lk(s_mu);
		c = ++s_count[key];
	}
	if (c <= 5 || c % 100 == 0) {
		std::fprintf(stderr, "[!!][Server][Reject] %s entity=%u reason=\"%s\" count=%u %s\n",
		             kind, eid, reason, c, detail ? detail : "");
		std::fflush(stderr);
	}
}

// Legacy shim (Move-only paths) — same throttle, prefixes kind="Move".
inline void logMoveReject(EntityId eid, const char* reason, const char* detail) {
	logActionReject("Move", eid, reason, detail);
}

inline const char* rejectCodeName(ActionRejectCode c) {
	switch (c) {
	case ActionRejectCode::ValueConservationViolated: return "ValueConservationViolated";
	case ActionRejectCode::ItemNotInInventory:        return "ItemNotInInventory";
	case ActionRejectCode::SourceEntityGone:          return "SourceEntityGone";
	case ActionRejectCode::SourceBlockGone:           return "SourceBlockGone";
	case ActionRejectCode::SourceBlockTypeMismatch:   return "SourceBlockTypeMismatch";
	case ActionRejectCode::PlacementTargetOccupied:   return "PlacementTargetOccupied";
	case ActionRejectCode::UnknownBlockType:          return "UnknownBlockType";
	case ActionRejectCode::ChunkNotLoaded:            return "ChunkNotLoaded";
	case ActionRejectCode::PickupOutOfRange:          return "PickupOutOfRange";
	case ActionRejectCode::TargetEntityGone:          return "TargetEntityGone";
	case ActionRejectCode::TargetHasNoInventory:      return "TargetHasNoInventory";
	case ActionRejectCode::ActorHasNoInventory:       return "ActorHasNoInventory";
	case ActionRejectCode::MissingItemId:             return "MissingItemId";
	}
	return "Unknown";
}

// Server-side callbacks — used by dedicated server (main_server.cpp) to broadcast
// world state changes to all connected clients over TCP.
struct ServerCallbacks {
	std::function<void(glm::ivec3 pos, BlockId oldBid, BlockId newBid, uint8_t p2)> onBlockChange; // block placed/broken
	std::function<void(EntityId id)> onEntityRemove;                     // entity despawned
	std::function<void(EntityId id, const Inventory&)> onInventoryChange; // inventory updated
};

// All server tuning constants in one header
#include "server/server_tuning.h"

struct ServerConfig {
	int seed = 42;
	int templateIndex = 1;  // VillageWorld (has trees)
	int port = 7777;
	float hpRegenInterval = ServerTuning::hpRegenInterval;  // seconds per +1 HP regen tick
	WorldGenConfig worldGenConfig;
};

class GameServer {
public:
	// Merge Python-declared feature tags into registered EntityDefs.
	void mergeArtifactTags(const std::vector<std::pair<std::string, std::vector<std::string>>>& tagsByType) {
		m_world->entities.mergeArtifactTags(tagsByType);
	}

	// Initialize world only (no entity spawning) — used by loadWorld
	void initWorld(const ServerConfig& config,
	               const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		auto tmpl = (config.templateIndex < (int)templates.size())
			? templates[config.templateIndex] : templates[0];

		m_world = std::make_unique<World>(config.seed, tmpl, config.templateIndex);
		m_wgc = config.worldGenConfig;
		m_hpRegenInterval = config.hpRegenInterval;
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

		// Load structure blueprints from artifacts/structures/
		m_blueprints.loadAll("artifacts/structures");
	}

	// Initialize server with world + world-scope static entities (chests).
	// Mobs are NOT spawned here — they spawn per-client in spawnMobsForClient()
	// when a GUI client connects. Chests are world-scope (persist across
	// sessions, owner=0, never despawn).
	void init(const ServerConfig& config,
	          const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		initWorld(config, templates);

		auto& tmpl = m_world->getTemplate();

		// Place chest blocks + matching Structure entities in all non-barn
		// houses. Villager behaviors discover these dynamically via
		// scan_entities("chest") — no per-villager chest wiring.
		auto houseChests = tmpl.houseChestPositions(m_world->seed());
		BlockId chestId = m_world->blocks.getId(BlockType::Chest);
		int chestCount = 0;
		for (auto& cPos : houseChests) {
			int cx = (int)std::round(cPos.x), cy = (int)std::round(cPos.y), cz = (int)std::round(cPos.z);
			if (chestId != BLOCK_AIR) {
				ChunkPos cp = worldToChunk(cx, cy, cz);
				Chunk* c = m_world->getChunk(cp);
				if (c) c->set(((cx%16)+16)%16, ((cy%16)+16)%16, ((cz%16)+16)%16, chestId);
			}
			glm::vec3 blockCenter = {(float)cx + 0.5f, (float)cy + 0.5f, (float)cz + 0.5f};
			EntityId chestEid = m_world->entities.spawn(StructureName::Chest, blockCenter, {});
			printf("[Server] Chest entity %u at (%d,%d,%d)\n", chestEid, cx, cy, cz);
			chestCount++;
		}

		printf("[Server] Initialized. Spawn: %.0f, %.0f, %.0f  Chests: %d houses\n",
		       m_spawnPos.x, m_spawnPos.y, m_spawnPos.z, chestCount);
	}

	// Spawn the mob set for one connecting client. Every mob gets
	// Prop::Owner = ownerId baked in, so the agent hosted by that client
	// can drive them and so disconnect cleanup can despawn them.
	void spawnMobsForClient(EntityId ownerId) {
		if (!m_world) return;
		auto& tmpl = m_world->getTemplate();
		auto& wgc  = m_wgc;

		// Finds the standing surface at (x,z): starts at terrain noise height
		// and walks up through built-up solid blocks (piled dirt, paths,
		// house floors) until the first air block. Returns that air Y —
		// i.e. where a mob's feet would rest. We do NOT keep scanning for
		// the topmost solid in the column, which would land mobs on ROOFTOPS
		// whenever a house covers (x,z). surfaceHeight() is terrain-only, so
		// we stay grounded even directly under a placed building.
		auto actualSurfaceY = [&](float x, float z) -> float {
			float terrainY = m_world->surfaceHeight(x, z);
			int bx = (int)std::round(x), bz = (int)std::round(z);
			int y = std::max(0, (int)std::round(terrainY));
			for (int i = 0; i < 8; i++) {
				BlockId bid = m_world->getBlock(bx, y, bz);
				if (!m_world->blocks.get(bid).solid) break;
				y++;
			}
			return (float)y;
		};
		auto safeSpawnHeight = [&](float x, float z) {
			return actualSurfaceY(x, z) + ServerTuning::spawnHeightOffset;
		};

		// Village center from template (virtual — works for any template type)
		auto vc = tmpl.villageCenter(m_world->seed());
		float mobCX = (float)vc.x, mobCZ = (float)vc.y;

		// Build mob list from wgc.mobs (seeded from Python template by the GUI,
		// possibly edited by the user). If empty (dedicated server, no GUI),
		// fall back to the Python template's mob config directly.
		std::vector<MobSpawn> mobList = wgc.mobs;
		if (mobList.empty()) {
			for (auto& mc : tmpl.pyConfig().mobs)
				mobList.push_back({mc.type, mc.count, mc.radius,
					parseSpawnAnchor(mc.spawnAt), mc.yOffset, mc.props});
		}

		// Resolve where each SpawnAnchor positions a mob group in world XZ.
		// Returns (cx, cz) anchor point and (spacing, insideBuilding):
		//   insideBuilding=true distributes entities in a grid inside the
		//   building (barn). Otherwise they're placed on a circular ring.
		auto portalSpawn = tmpl.preferredSpawn(m_world->seed());
		auto barnCtr     = tmpl.barnCenter(m_world->seed());
		int  barnFY      = tmpl.barnFloorY(m_world->seed());
		int barnSlot = 0;  // flat counter across all species so no two land on the same cell
		struct AnchorInfo { float cx, cz, defaultRadius; bool inside; float fixedY; };
		auto resolveAnchor = [&](SpawnAnchor a) -> AnchorInfo {
			switch (a) {
			case SpawnAnchor::Monument:
				return {mobCX, mobCZ, 6.0f, false, -1.0f};
			case SpawnAnchor::Barn:
				if (barnCtr.x >= 0)
					return {(float)barnCtr.x, (float)barnCtr.y, 4.0f, true, (float)barnFY};
				return {mobCX, mobCZ, 10.0f, false, -1.0f};  // no barn → fall back to ring
			case SpawnAnchor::Portal:
				return {portalSpawn.x, portalSpawn.z, 3.0f, false, -1.0f};
			case SpawnAnchor::VillageCenter:
			default:
				return {mobCX, mobCZ, wgc.mobSpawnRadius, false, -1.0f};
			}
		};

		auto spawnOne = [&](const MobSpawn& ms, float x, float z, float fixedY,
		                    std::unordered_map<std::string, PropValue> extraProps) {
			for (auto& [k, v] : ms.props) extraProps[k] = v;
			auto bIt = wgc.behaviorOverrides.find(ms.typeId);
			if (bIt != wgc.behaviorOverrides.end())
				extraProps[Prop::BehaviorId] = bIt->second;
			// Every mob is owned by the connecting client so their in-process
			// AgentClient can drive it and so logout cleanup is straightforward.
			extraProps[Prop::Owner] = (int)ownerId;
			float y = (fixedY >= 0.0f) ? fixedY : safeSpawnHeight(x, z);
			y += ms.yOffset;
			EntityId eid = m_world->entities.spawn(ms.typeId, {x, y, z}, extraProps);
			auto iIt = wgc.startingItems.find(ms.typeId);
			if (iIt != wgc.startingItems.end()) {
				Entity* e = m_world->entities.get(eid);
				if (e && e->inventory) {
					for (auto& [itemId, cnt] : iIt->second)
						e->inventory->add(itemId, cnt);
				}
			}
			return eid;
		};

		auto spawnMob = [&](const MobSpawn& ms, float baseOffset) {
			AnchorInfo a = resolveAnchor(ms.anchor);
			float radius = (ms.radius > 0) ? ms.radius : a.defaultRadius;
			for (int m = 0; m < ms.count; m++) {
				std::unordered_map<std::string, PropValue> extraProps;
				float ex, ez;
				float fixedY = a.fixedY;
				if (a.inside) {
					int slotIdx = barnSlot++;
					int gx = slotIdx % 6;
					int gz = (slotIdx / 6) % 4;
					ex = a.cx + (gx - 2.5f) * radius;
					ez = a.cz + (gz - 1.5f) * radius;
				} else {
					float angle = (float)m / (float)ms.count * 6.28318f + baseOffset;
					ex = a.cx + std::cos(angle) * radius;
					ez = a.cz + std::sin(angle) * radius;
				}
				spawnOne(ms, ex, ez, fixedY, std::move(extraProps));
			}
		};

		int mobsBefore = (int)m_world->entities.count();
		for (int i = 0; i < (int)mobList.size(); i++) {
			if (mobList[i].count <= 0) continue;
			spawnMob(mobList[i], (float)i);
		}
		int spawned = (int)m_world->entities.count() - mobsBefore;
		printf("[Server] Spawned %d mobs for owner=%u\n", spawned, ownerId);
	}

	// Add a client. Returns the player's EntityId.
	EntityId addClient(ClientId clientId, const std::string& characterSkin = "") {
		// Always spawn as base:player (has inventory, HP, physics).
		// The characterSkin determines visual model, not entity type.
		EntityId eid = m_world->entities.spawn(LivingName::Player, m_spawnPos);
		Entity* pe = m_world->entities.get(eid);
		if (pe) {
			// Default yaw: +Z (toward stairs and village).
			pe->yaw = 90.0f;
			// When enabled and the template has a village/monument, orient
			// the player toward the monument so they spawn already looking
			// through the gateway arch at the village center.
			if (m_wgc.playerFacesMonument && m_world) {
				auto vc = m_world->getTemplate().villageCenter(m_world->seed());
				float dx = (float)vc.x - m_spawnPos.x;
				float dz = (float)vc.y - m_spawnPos.z;
				if (dx * dx + dz * dz > 0.1f) {
					// Yaw convention: 0=+X, 90=+Z (see gameplay_movement.cpp).
					// atan2(dz, dx) gives the angle from +X to the
					// gateway→monument vector.
					pe->yaw = glm::degrees(std::atan2(dz, dx));
				}
			}
		}
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
				auto sit = m_wgc.startingItems.find(LivingName::Player);
				if (sit != m_wgc.startingItems.end()) {
					for (auto& [item, count] : sit->second)
						pe->inventory->add(item, count);
				} else {
					pe->inventory->add(BlockType::Stone, 10);
					pe->inventory->add(BlockType::Wood, 10);
					pe->inventory->add("sword", 1);
					pe->inventory->add("shield", 1);
					pe->inventory->add("potion", 3);
				}
			}
		}
		m_clients[clientId] = {eid};
		printf("[Server] Client %u joined. Player entity: %u\n", clientId, eid);

		// Spawn this client's mob set. Every mob is owned by the player entity
		// just created, so the client's in-process AgentClient can drive them
		// and so removeClient() can despawn them on disconnect.
		//
		// Prior sessions leave a per-skin snapshot behind (see removeClient).
		// If one exists, restore from that instead of respawning fresh — a
		// dog you led to the far hills will still be at the far hills next
		// login, not warped back to the village.
		std::string skin = characterSkin.empty() ? "default" : characterSkin;
		if (!m_ownedEntities.restore(m_world->entities, eid, skin))
			spawnMobsForClient(eid);

		return eid;
	}

	// NOTE: Agent-specific APIs removed (addAgentClient, assignEntityToClient,
	// revokeEntityFromClient, getControlledEntities, getUncontrolledNPCs).
	// AgentClient now runs inside PlayerClient — no server-side agent management.

	// Check if a client is allowed to control an entity.
	// Rules: (1) GUI clients can control entities they own (Prop::Owner matches their player),
	//        (2) admin clients (player has fly_mode) can control any entity.
	bool canClientControl(ClientId clientId, EntityId targetId) const {
		auto cit = m_clients.find(clientId);
		if (cit == m_clients.end()) return false;
		// Admin: fly_mode on their player entity
		Entity* playerEnt = m_world->entities.get(cit->second.playerEntityId);
		if (playerEnt && playerEnt->getProp<bool>("fly_mode", false))
			return true;
		// Normal: target must be owned by this client's player
		Entity* target = m_world->entities.get(targetId);
		if (!target) return false;
		int ownerId = target->getProp<int>(Prop::Owner, 0);
		return ownerId == (int)cit->second.playerEntityId;
	}

	void removeClient(ClientId clientId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end()) {
			EntityId playerId = it->second.playerEntityId;
			// Save player inventory before removing
			if (playerId != ENTITY_NONE) {
				Entity* pe = m_world->entities.get(playerId);
				std::string skin = "default";
				if (pe) skin = pe->getProp<std::string>("character_skin", "default");
				if (pe && pe->inventory) {
					m_savedInventories[skin] = *pe->inventory;
					printf("[Server] Saved inventory for '%s'\n", skin.c_str());
				}
				// Snapshot every owned NPC (not the player itself) so next
				// login restores them in place instead of respawning fresh
				// from the template. Dead entities and unknown types are
				// dropped, not snapshotted — see OwnedEntityStore::snapshot.
				m_ownedEntities.snapshot(m_world->entities, playerId, skin);

				// Despawn every entity owned by this client (their mob set).
				// Entities with owner=0 (static chests, etc.) are world-scope
				// and stay. Self-ownership of the player entity matches here
				// too, so the player itself gets removed in the same pass.
				int despawned = 0;
				m_world->entities.forEach([&](Entity& e) {
					if (e.getProp<int>(Prop::Owner, 0) == (int)playerId) {
						m_world->entities.remove(e.id());
						despawned++;
					}
				});
				printf("[Server] Despawned %d entities owned by player %u\n",
					despawned, playerId);
			}
			m_clients.erase(it);
			printf("[Server] Client %u disconnected.\n", clientId);
		}
	}

	// Submit action directly without ownership check (test-only).
	void receiveActionDirect(const ActionProposal& action) {
		m_world->proposals.propose(action);
	}

	void receiveAction(ClientId clientId, ActionProposal action) {
		m_actionStats.received++;
		switch (action.type) {
		case ActionProposal::Move:     m_actionStats.moveRecv++;     break;
		case ActionProposal::Relocate: m_actionStats.relocateRecv++; break;
		case ActionProposal::Convert:  m_actionStats.convertRecv++;  break;
		case ActionProposal::Interact: m_actionStats.interactRecv++; break;
		}
		auto it = m_clients.find(clientId);
		if (it == m_clients.end()) {
			m_actionStats.rejected++;
			if (action.type == ActionProposal::Move) {
				char buf[96];
				std::snprintf(buf, sizeof(buf), "unknown client=%u", clientId);
				logMoveReject(action.actorId, "unknown-client", buf);
			}
			return;
		}

		if (!canClientControl(clientId, action.actorId)) {
			m_actionStats.rejected++;
			if (action.type == ActionProposal::Move) {
				Entity* t = m_world->entities.get(action.actorId);
				char buf[160];
				if (!t) {
					std::snprintf(buf, sizeof(buf),
						"actor entity=%u not found (client=%u)",
						action.actorId, clientId);
				} else {
					int ownerId = t->getProp<int>(Prop::Owner, 0);
					std::snprintf(buf, sizeof(buf),
						"owner=%d but client player=%u (client=%u)",
						ownerId, it->second.playerEntityId, clientId);
				}
				logMoveReject(action.actorId, "ownership-check-failed", buf);
			}
			return;
		}

		// Goal text: update server-side entity for broadcast
		if (!action.goalText.empty()) {
			Entity* e = m_world->entities.get(action.actorId);
			if (e && e->goalText != action.goalText) {
				e->goalText = action.goalText;
			}
		}

		m_world->proposals.propose(action);
	}

	// Set callbacks for visual effects (client provides these)
	void setCallbacks(ServerCallbacks cb) { m_callbacks = cb; }
	ServerCallbacks& callbacks() { return m_callbacks; }

	// Main server tick
	void tick(float dt) {
#ifdef CIVCRAFT_PERF
		using Clock = std::chrono::steady_clock;
		auto phaseStart = Clock::now();
		auto tickStart = phaseStart;
		auto markPhase = [&](double& slot) {
			auto now = Clock::now();
			slot = std::chrono::duration<double, std::milli>(now - phaseStart).count();
			phaseStart = now;
		};
#else
		// No-op so call sites stay clean. Compiler eliminates the call.
		auto markPhase = [](double&) {};
#endif

		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& def = m_world->blocks.get(m_world->getBlock(x, y, z));
			return def.solid ? def.collision_height : 0.0f;
		};

		// AI behavior decisions arrive as ActionProposals from bot client
		// processes — no server-side AI gathering needed.

		// Phase 1: Resolve all proposals (may set entity.removed = true)
		resolveActions(dt);
		markPhase(m_lastTickProfile.resolveActionsMs);

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
		markPhase(m_lastTickProfile.navigationMs);

		// Physics for all entities (purges removed entities from the map)
		m_world->entities.stepPhysics(dt, solidFn);
		markPhase(m_lastTickProfile.physicsMs);

		// Smooth body yaw toward direction of travel for every entity.
		// Single authoritative source — replaces per-event yaw snaps in
		// server.cpp and pathfind.h so NPCs turn naturally instead of
		// flipping instantly when velocity reverses.
		m_world->entities.forEach([&](Entity& e) {
			if (e.removed) return;
			smoothYawTowardsVelocity(e.yaw, e.velocity, dt);
		});
		markPhase(m_lastTickProfile.yawSmoothMs);

		// Active block ticking (TNT, wheat, wire)
		m_activeBlockTimer += dt;
		if (m_activeBlockTimer >= 0.05f) {
			m_world->tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId bid) {
				glm::ivec3 pos{bx, by, bz};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pos, BLOCK_AIR, bid, 0);
			});
			m_activeBlockTimer = 0;
		}
		markPhase(m_lastTickProfile.activeBlocksMs);

		// Item pickup is CLIENT-INITIATED: clients send PickupItem actions.
		// Server validates and executes in resolveActions().
		// Creatures pickup is handled by Python behavior → PickupItem action.

		// HP regeneration: all Living entities regen +1 HP per tick (configurable interval)
		m_regenTimer += dt;
		if (m_regenTimer >= m_hpRegenInterval) {
			m_regenTimer = 0;
			m_world->entities.forEach([&](Entity& e) {
				if (!e.def().isLiving()) return;
				if (e.removed) return;
				int hp = e.hp();
				int maxHp = e.def().max_hp;
				if (maxHp > 0 && hp > 0 && hp < maxHp) {
					e.setHp(hp + 1);
				}
			});
		}
		markPhase(m_lastTickProfile.hpRegenMs);

		// Structure regeneration: scan dirty set, place one missing block per structure per interval
		m_structureRegenTimer += dt;
		if (m_structureRegenTimer >= ServerTuning::structureRegenCheckInterval) {
			float elapsed = m_structureRegenTimer;
			m_structureRegenTimer = 0;

			auto blockAtFn = [&](int x, int y, int z) -> std::string {
				return m_world->blocks.get(m_world->getBlock(x, y, z)).string_id;
			};

			for (auto it = m_incompleteStructures.begin(); it != m_incompleteStructures.end(); ) {
				EntityId id = *it;
				Entity* e = m_world->entities.get(id);
				if (!e || e->removed || !e->structure) {
					it = m_incompleteStructures.erase(it);
					continue;
				}
				const auto* bp = m_blueprints.get(e->structure->blueprintId);
				if (!bp) { it = m_incompleteStructures.erase(it); continue; }

				auto missing = m_blueprints.firstMissingBlock(*e, blockAtFn);
				if (!missing) {
					// Fully healed
					it = m_incompleteStructures.erase(it);
					continue;
				}

				if (bp->regenerates) {
					e->structure->regenTimer += elapsed;
					if (e->structure->regenTimer >= bp->regen_interval_s) {
						e->structure->regenTimer = 0;
						glm::ivec3 wp = e->structure->anchorPos + missing->offset;
						BlockId bid = m_world->blocks.getId(missing->block_type);
						if (bid != BLOCK_AIR) {
							ChunkPos cp = worldToChunk(wp.x, wp.y, wp.z);
							Chunk* c = m_world->getChunk(cp);
							if (c) {
								c->set(((wp.x % 16) + 16) % 16,
								       ((wp.y % 16) + 16) % 16,
								       ((wp.z % 16) + 16) % 16, bid);
								if (m_callbacks.onBlockChange)
									m_callbacks.onBlockChange(wp, BLOCK_AIR, bid, 0);
							}
						}
					}
				}
				++it;
			}
		}
		markPhase(m_lastTickProfile.structureRegenMs);

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
		markPhase(m_lastTickProfile.stuckDetectionMs);

#ifdef CIVCRAFT_PERF
		m_lastTickProfile.totalMs =
			std::chrono::duration<double, std::milli>(Clock::now() - tickStart).count();
#endif
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

	// Per-character owned-NPC snapshots. Written on disconnect, consumed on
	// next login. Exposed for world_save.h so snapshots survive across
	// server restarts (owned_entities.bin).
	OwnedEntityStore&       ownedEntities()       { return m_ownedEntities; }
	const OwnedEntityStore& ownedEntities() const { return m_ownedEntities; }

	EntityId getPlayerEntity(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		return it != m_clients.end() ? it->second.playerEntityId : ENTITY_NONE;
	}

public:
	// Action-proposal counters — reset externally by the status log every 2s
	// so we can confirm the server is actually receiving and resolving the
	// C_ACTION messages the client claims to have sent. Missing the "recv"
	// side of this pair means the TCP stream is delivering but the server
	// rejected before queueing (ownership/unknown-client).
	struct ActionStats {
		int received = 0;
		int rejected = 0;
		int resolved = 0;
		// Per-type breakdown of received proposals so [ServerAlive] can show
		// WHICH kind of action (move/relocate/convert/interact) is pouring in,
		// and whether any specific type is disproportionately rejected.
		int moveRecv     = 0;
		int relocateRecv = 0;
		int convertRecv  = 0;
		int interactRecv = 0;
	};
	ActionStats& actionStats() { return m_actionStats; }

	// Per-phase timing for the most recent tick(). Filled by tick() each call
	// so main_server.cpp can print a breakdown when a tick exceeds budget.
	struct TickProfile {
		double resolveActionsMs   = 0.0;
		double navigationMs       = 0.0;
		double physicsMs          = 0.0;
		double yawSmoothMs        = 0.0;
		double activeBlocksMs     = 0.0;
		double hpRegenMs          = 0.0;
		double structureRegenMs   = 0.0;
		double stuckDetectionMs   = 0.0;
		double totalMs            = 0.0;
	};
	const TickProfile& lastTickProfile() const { return m_lastTickProfile; }

private:
	void resolveActions(float dt);

	ActionStats m_actionStats;
	TickProfile m_lastTickProfile;
	std::unique_ptr<World> m_world;
	ServerCallbacks m_callbacks;
	WorldGenConfig m_wgc;
	float m_worldTime = 0.30f;
	float m_activeBlockTimer = 0;
	float m_stuckTimer = 0;
	float m_regenTimer = 0;
	float m_hpRegenInterval = ServerTuning::hpRegenInterval;
	glm::vec3 m_spawnPos = {30, 10, 30};
	std::unordered_map<std::string, Inventory> m_savedInventories;  // character_skin → inventory
	OwnedEntityStore m_ownedEntities;  // character_skin → owned NPCs awaiting owner's next login

	// Structure system
	StructureBlueprintManager            m_blueprints;
	StructureBlockCacher                 m_structureCacher;
	std::unordered_set<EntityId>         m_incompleteStructures;  // dirty set: structures with missing blocks
	float                                m_structureRegenTimer = 0;

	// Stuck detection: last known position per entity (checked every stuckCheckInterval)
	std::unordered_map<EntityId, glm::vec3> m_lastPositions;

	// After rejecting a clientPos for an entity (wall-phase, etc.) we drop
	// `hasClientPos` on its Move actions for this many ticks. Reason: a handful
	// of stale proposals are typically in the TCP pipe before the client's
	// next frame processes S_BLOCK and sees the new world state; rejecting
	// each one individually would spam the log. Counter is decremented once
	// per tick in resolveActions. ~10 ticks ≈ 167ms at 60tps — a round trip.
	std::unordered_map<EntityId, int> m_clientPosRejectCooldown;

	struct ClientState {
		EntityId playerEntityId = ENTITY_NONE;
	};
	std::unordered_map<ClientId, ClientState> m_clients;
};

} // namespace civcraft

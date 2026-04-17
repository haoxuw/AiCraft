#pragma once

// GameServer — authoritative world simulation. Owns World, runs 3-phase tick,
// validates ActionProposals. Always over TCP (same code path for singleplayer
// and dedicated).

#include "server/world.h"
#include "server/entity_manager.h"
#include "server/world_gen_config.h"
#include "logic/action.h"
#include "logic/constants.h"
#include "logic/physics.h"
#include "server/world_template.h"
#include "server/pathfind.h"
#include "server/structure_blueprint.h"
#include "server/structure_block_cacher.h"
#include "server/weather.h"
#include "logic/block_registry.h"
// Must precede `namespace civcraft {` — system headers can't land inside.
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

// ClientId lives in shared/types.h.

// Rejection codes for action proposals; uint32_t for cheap log filtering.
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

// First 5 hits immediate, then every 100th. Localhost singleplayer should
// never see any — each one is a bug. "[!!]" prefix stands out.
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

// Legacy shim (Move-only paths); same throttle.
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

// TCP broadcast hooks. Rule 5: nothing display-related here.
struct ServerCallbacks {
	std::function<void(glm::ivec3 pos, BlockId oldBid, BlockId newBid, uint8_t p2)> onBlockChange;
	std::function<void(EntityId id)> onEntityRemove;
	std::function<void(EntityId id, const Inventory&)> onInventoryChange;
};

#include "server/server_tuning.h"

struct ServerConfig {
	int seed = 42;
	int templateIndex = 1;  // VillageWorld (has trees)
	int port = 7777;
	float hpRegenInterval = ServerTuning::hpRegenInterval;
	WorldGenConfig worldGenConfig;
};

class GameServer {
public:
	void mergeArtifactTags(const std::vector<std::pair<std::string, std::vector<std::string>>>& tagsByType) {
		m_world->entities.mergeArtifactTags(tagsByType);
	}

	// World only (no entities) — used by loadWorld.
	void initWorld(const ServerConfig& config,
	               const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		auto tmpl = (config.templateIndex < (int)templates.size())
			? templates[config.templateIndex] : templates[0];

		m_world = std::make_unique<World>(config.seed, tmpl, config.templateIndex);
		m_wgc = config.worldGenConfig;
		m_hpRegenInterval = config.hpRegenInterval;
		m_worldTime = 0.26f;  // just past sunrise
		// Rule 1: Python-configurable; default 1200s / 20min.
		m_dayLengthTicks = tmpl->pyConfig().dayLengthTicks;

		// Empty → "clear" default baked into WeatherPyConfig.
		WeatherPyConfig wcfg;
		if (!tmpl->pyConfig().weatherSchedule.empty()) {
			std::string wpath = std::string("artifacts/")
			                  + tmpl->pyConfig().weatherSchedule;
			loadWeatherSchedule(wpath, wcfg);
		}
		m_weather.load(wcfg, (uint32_t)config.seed);

		glm::vec3 rawSpawn = tmpl->preferredSpawn(config.seed);
		float sx = rawSpawn.x, sz = rawSpawn.z;

		// Escape any structure/tree at spawn by scanning up.
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

		m_blueprints.loadAll("artifacts/structures");
	}

	// World-scope entities only (chests, monument; owner=0, permanent).
	// Mobs spawn per-client in spawnMobsForClient() on connect.
	void init(const ServerConfig& config,
	          const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		initWorld(config, templates);

		auto& tmpl = m_world->getTemplate();

		// Chests in non-barn houses. Villagers find via scan_entities("chest").
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

		// Client handle for flame FX; tower blocks live in world_template.
		glm::vec3 mPos = tmpl.monumentPosition(m_world->seed());
		if (mPos.y >= 0) {
			EntityId mEid = m_world->entities.spawn(StructureName::Monument, mPos, {});
			printf("[Server] Monument entity %u at (%.1f,%.1f,%.1f)\n",
			       mEid, mPos.x, mPos.y, mPos.z);
		}

		printf("[Server] Initialized. Spawn: %.0f, %.0f, %.0f  Chests: %d houses\n",
		       m_spawnPos.x, m_spawnPos.y, m_spawnPos.z, chestCount);
	}

	// Rule 4: each mob gets Prop::Owner=ownerId for client-hosted agent + cleanup.
	void spawnMobsForClient(EntityId ownerId) {
		if (!m_world) return;
		auto& tmpl = m_world->getTemplate();
		auto& wgc  = m_wgc;

		// Walk terrain → paths/floors → first air Y. Must NOT find topmost
		// solid or mobs land on rooftops where houses cover (x,z).
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

		auto vc = tmpl.villageCenter(m_world->seed());
		float mobCX = (float)vc.x, mobCZ = (float)vc.y;

		// GUI populates wgc.mobs from Python template (may be edited).
		// Dedicated server has no GUI → fall back to template directly.
		std::vector<MobSpawn> mobList = wgc.mobs;
		if (mobList.empty()) {
			for (auto& mc : tmpl.pyConfig().mobs)
				mobList.push_back({mc.type, mc.count, mc.radius,
					parseSpawnAnchor(mc.spawnAt), mc.yOffset, mc.props});
		}

		// inside=true → grid in barn; else → circular ring.
		auto portalSpawn = tmpl.preferredSpawn(m_world->seed());
		auto barnCtr     = tmpl.barnCenter(m_world->seed());
		int  barnFY      = tmpl.barnFloorY(m_world->seed());
		int barnSlot = 0;  // cross-species counter (avoid cell collisions)
		struct AnchorInfo { float cx, cz, defaultRadius; bool inside; float fixedY; };
		auto resolveAnchor = [&](SpawnAnchor a) -> AnchorInfo {
			switch (a) {
			case SpawnAnchor::Monument:
				return {mobCX, mobCZ, 6.0f, false, -1.0f};
			case SpawnAnchor::Barn:
				if (barnCtr.x >= 0)
					return {(float)barnCtr.x, (float)barnCtr.y, 4.0f, true, (float)barnFY};
				return {mobCX, mobCZ, 10.0f, false, -1.0f};  // no barn → ring
			case SpawnAnchor::Portal:
				return {portalSpawn.x, portalSpawn.z, 3.0f, false, -1.0f};
			case SpawnAnchor::VillageCenter:
			default:
				return {mobCX, mobCZ, wgc.mobSpawnRadius, false, -1.0f};
			}
		};

		// actualSurfaceY can stop at an air gap inside a tree canopy.
		auto hasHeadroom = [&](float x, float y, float z) -> bool {
			int bx = (int)std::floor(x), bz = (int)std::floor(z);
			int by = (int)std::floor(y);
			for (int dy = 0; dy <= 1; dy++) {
				const auto& def = m_world->blocks.get(m_world->getBlock(bx, by + dy, bz));
				if (def.solid) return false;
			}
			return true;
		};

		auto spawnOne = [&](const MobSpawn& ms, float x, float z, float fixedY,
		                    std::unordered_map<std::string, PropValue> extraProps) {
			for (auto& [k, v] : ms.props) extraProps[k] = v;
			auto bIt = wgc.behaviorOverrides.find(ms.typeId);
			if (bIt != wgc.behaviorOverrides.end())
				extraProps[Prop::BehaviorId] = bIt->second;
			// Owner → hosted AgentClient drives it; logout cleanup finds by owner.
			extraProps[Prop::Owner] = (int)ownerId;
			float y = (fixedY >= 0.0f) ? fixedY : safeSpawnHeight(x, z);
			y += ms.yOffset;

			// Spiral 16 cells to escape a single tree; fallback = original.
			if (!hasHeadroom(x, y, z)) {
				static const float kSpiral[16][2] = {
					{ 1, 0},{ 0, 1},{-1, 0},{ 0,-1},
					{ 2, 0},{ 0, 2},{-2, 0},{ 0,-2},
					{ 2, 2},{-2, 2},{-2,-2},{ 2,-2},
					{ 3, 0},{ 0, 3},{-3, 0},{ 0,-3},
				};
				for (auto& off : kSpiral) {
					float nx = x + off[0], nz = z + off[1];
					float ny = (fixedY >= 0.0f) ? fixedY : safeSpawnHeight(nx, nz);
					ny += ms.yOffset;
					if (hasHeadroom(nx, ny, nz)) { x = nx; z = nz; y = ny; break; }
				}
			}

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

	// Returns player's EntityId. characterSkin is visual-only.
	EntityId addClient(ClientId clientId, const std::string& characterSkin = "") {
		EntityId eid = m_world->entities.spawn(LivingName::Player, m_spawnPos);
		Entity* pe = m_world->entities.get(eid);
		if (pe) {
			pe->yaw = 90.0f;  // +Z toward stairs
			if (m_wgc.playerFacesMonument && m_world) {
				auto vc = m_world->getTemplate().villageCenter(m_world->seed());
				float dx = (float)vc.x - m_spawnPos.x;
				float dz = (float)vc.y - m_spawnPos.z;
				if (dx * dx + dz * dz > 0.1f) {
					// Yaw: 0=+X, 90=+Z (see gameplay_movement.cpp).
					pe->yaw = glm::degrees(std::atan2(dz, dx));
				}
			}
		}
		// Self-owned so server click-to-move can drive the player.
		if (pe) pe->setProp(Prop::Owner, (int)eid);
		if (pe && !characterSkin.empty())
			pe->setProp("character_skin", characterSkin);
		if (pe && pe->inventory) {
			std::string skin = characterSkin.empty() ? "default" : characterSkin;
			auto savedIt = m_savedInventories.find(skin);
			if (savedIt != m_savedInventories.end()) {
				*pe->inventory = savedIt->second;
				printf("[Server] Restored saved inventory for '%s'\n", skin.c_str());
			} else {
				// First-time starter kit.
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

		// Per-skin snapshot restore so owned NPCs keep their last position.
		std::string skin = characterSkin.empty() ? "default" : characterSkin;
		if (!m_ownedEntities.restore(m_world->entities, eid, skin))
			spawnMobsForClient(eid);

		return eid;
	}

	// Clients control owned entities, or any entity if player has fly_mode (admin).
	bool canClientControl(ClientId clientId, EntityId targetId) const {
		auto cit = m_clients.find(clientId);
		if (cit == m_clients.end()) return false;
		Entity* playerEnt = m_world->entities.get(cit->second.playerEntityId);
		if (playerEnt && playerEnt->getProp<bool>("fly_mode", false))
			return true;
		Entity* target = m_world->entities.get(targetId);
		if (!target) return false;
		int ownerId = target->getProp<int>(Prop::Owner, 0);
		return ownerId == (int)cit->second.playerEntityId;
	}

	void removeClient(ClientId clientId) {
		auto it = m_clients.find(clientId);
		if (it != m_clients.end()) {
			EntityId playerId = it->second.playerEntityId;
			if (playerId != ENTITY_NONE) {
				Entity* pe = m_world->entities.get(playerId);
				std::string skin = "default";
				if (pe) skin = pe->getProp<std::string>("character_skin", "default");
				if (pe && pe->inventory) {
					m_savedInventories[skin] = *pe->inventory;
					printf("[Server] Saved inventory for '%s'\n", skin.c_str());
				}
				// Snapshot owned NPCs for restore next login (dead/unknown dropped).
				m_ownedEntities.snapshot(m_world->entities, playerId, skin);

				// Player is self-owned so this pass also despawns them.
				// World-scope entities (owner=0) stay.
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

	// Test-only: no ownership check.
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

		// Server-side goalText for broadcast.
		if (!action.goalText.empty()) {
			Entity* e = m_world->entities.get(action.actorId);
			if (e && e->goalText != action.goalText) {
				e->goalText = action.goalText;
			}
		}

		m_world->proposals.propose(action);
	}

	void setCallbacks(ServerCallbacks cb) { m_callbacks = cb; }
	ServerCallbacks& callbacks() { return m_callbacks; }

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
		auto markPhase = [](double&) {};  // compiler elides
#endif

		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& def = m_world->blocks.get(m_world->getBlock(x, y, z));
			return def.solid ? def.collision_height : 0.0f;
		};

		// Rule 4: AI arrives as ActionProposals from agent clients.
		// Phase 1: resolve (may set entity.removed=true).
		resolveActions(dt);
		markPhase(m_lastTickProfile.resolveActionsMs);

		// Broadcast removals BEFORE stepPhysics erases them.
		if (m_callbacks.onEntityRemove) {
			m_world->entities.forEachIncludingRemoved([&](Entity& e) {
				if (e.removed && !e.removalBroadcast) {
					m_callbacks.onEntityRemove(e.id());
					e.removalBroadcast = true;
				}
			});
		}

		updateNavigation(dt, m_world->entities);
		markPhase(m_lastTickProfile.navigationMs);

		// Also purges removed entities.
		m_world->entities.stepPhysics(dt, solidFn);
		markPhase(m_lastTickProfile.physicsMs);

		// Single authoritative yaw smoothing → natural turns on reversal.
		m_world->entities.forEach([&](Entity& e) {
			if (e.removed) return;
			smoothYawTowardsVelocity(e.yaw, e.velocity, dt);
		});
		markPhase(m_lastTickProfile.yawSmoothMs);

		// TNT, wheat, wire.
		m_activeBlockTimer += dt;
		if (m_activeBlockTimer >= 0.05f) {
			m_world->tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId bid) {
				glm::ivec3 pos{bx, by, bz};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pos, BLOCK_AIR, bid, 0);
			});
			m_activeBlockTimer = 0;
		}
		markPhase(m_lastTickProfile.activeBlocksMs);

		// HP regen: +1 / m_hpRegenInterval for Living.
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

		// One missing block per structure per interval.
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

		// 1 unit = 1 full day; dayLengthTicks = real seconds per day.
		m_worldTime += (1.0f / (float)m_dayLengthTicks) * dt;
		if (m_worldTime >= 1.0f) m_worldTime -= std::floor(m_worldTime);

		// Wind updates every tick; kind/intensity on schedule.
		m_weather.tick(dt);

		m_stuckTimer += dt;
		if (m_stuckTimer >= ServerTuning::stuckCheckInterval) {
			m_stuckTimer = 0;

			m_world->entities.forEach([&](Entity& e) {
				if (!e.def().isLiving()) return;
				if (e.nav.active) return;  // nav handles its own stuck

				EntityId id = e.id();
				auto it = m_lastPositions.find(id);
				if (it == m_lastPositions.end()) {
					m_lastPositions[id] = e.position;
					return;
				}

				float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
				if (hSpeed < ServerTuning::stuckMinSpeed) {
					it->second = e.position;
					return;
				}

				float displacement = glm::length(glm::vec2(
					e.position.x - it->second.x, e.position.z - it->second.z));

				if (displacement < ServerTuning::stuckMaxDisplacement) {
					// Up + sideways; gravity drops them back down.
					float nudgeX = (e.velocity.x > 0 ? 1 : -1) * ServerTuning::unstuckNudgeHoriz;
					float nudgeZ = (e.velocity.z > 0 ? 1 : -1) * ServerTuning::unstuckNudgeHoriz;
					e.position.y += ServerTuning::unstuckNudgeHeight;
					e.position.x += nudgeX;
					e.position.z += nudgeZ;
					e.velocity.y = 0;
				}

				it->second = e.position;
			});

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

	World& world() { return *m_world; }
	const World& world() const { return *m_world; }
	float worldTime() const { return m_worldTime; }
	void setWorldTime(float t) { m_worldTime = t; }
	// Read by ClientManager::broadcastState via ServerInterface.
	const WeatherState& weather() const { return m_weather.state(); }
	WeatherController&  weatherController()       { return m_weather; }
	glm::vec3 spawnPos() const { return m_spawnPos; }
	void setSpawnPos(glm::vec3 p) { m_spawnPos = p; }
	const WorldGenConfig& worldGenConfig() const { return m_wgc; }

	std::unordered_map<std::string, Inventory>& savedInventories() { return m_savedInventories; }
	const std::unordered_map<std::string, Inventory>& savedInventories() const { return m_savedInventories; }

	// Exposed for world_save.h (owned_entities.bin survives restarts).
	OwnedEntityStore&       ownedEntities()       { return m_ownedEntities; }
	const OwnedEntityStore& ownedEntities() const { return m_ownedEntities; }

	EntityId getPlayerEntity(ClientId clientId) const {
		auto it = m_clients.find(clientId);
		return it != m_clients.end() ? it->second.playerEntityId : ENTITY_NONE;
	}

public:
	// Reset by status log every 2s. Missing "recv" = TCP ok but pre-queue
	// rejected (ownership/unknown-client).
	struct ActionStats {
		int received = 0;
		int rejected = 0;
		int resolved = 0;
		// Per-type for [ServerAlive] to spot disproportionate rejection.
		int moveRecv     = 0;
		int relocateRecv = 0;
		int convertRecv  = 0;
		int interactRecv = 0;
	};
	ActionStats& actionStats() { return m_actionStats; }

	// Filled by tick() so main can dump phase breakdown on overrun.
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
	int   m_dayLengthTicks = 1200;   // seeded from WorldPyConfig
	WeatherController m_weather;     // Markov chain (global)
	float m_activeBlockTimer = 0;
	float m_stuckTimer = 0;
	float m_regenTimer = 0;
	float m_hpRegenInterval = ServerTuning::hpRegenInterval;
	glm::vec3 m_spawnPos = {30, 10, 30};
	std::unordered_map<std::string, Inventory> m_savedInventories;  // skin → inventory
	OwnedEntityStore m_ownedEntities;  // skin → owned NPCs awaiting next login

	StructureBlueprintManager            m_blueprints;
	StructureBlockCacher                 m_structureCacher;
	std::unordered_set<EntityId>         m_incompleteStructures;  // dirty set
	float                                m_structureRegenTimer = 0;

	std::unordered_map<EntityId, glm::vec3> m_lastPositions;

	// Post-reject: drop hasClientPos on Moves for ~10 ticks (~167ms) to let
	// stale in-flight proposals observe the S_BLOCK update without spamming.
	// See feedback_client_pos_reject_policy.md.
	std::unordered_map<EntityId, int> m_clientPosRejectCooldown;

	struct ClientState {
		EntityId playerEntityId = ENTITY_NONE;
	};
	std::unordered_map<ClientId, ClientState> m_clients;
};

} // namespace civcraft

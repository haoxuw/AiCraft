#include "server/server.h"
#include <cmath>

namespace modcraft {

void GameServer::resolveActions(float dt) {
	auto proposals = m_world->proposals.drain();

	for (auto& p : proposals) {
		switch (p.type) {

		case ActionProposal::Move: {
			Entity* e = m_world->entities.get(p.actorId);
			if (!e) break;

			// Server validates: clamp to entity's max speed
			float maxSpeed = e->def().walk_speed;
			if (maxSpeed > 0) {
				float len = glm::length(glm::vec2(p.desiredVel.x, p.desiredVel.z));
				if (len > maxSpeed * 3.0f) {
					float scale = (maxSpeed * 3.0f) / len;
					p.desiredVel.x *= scale;
					p.desiredVel.z *= scale;
				}
			}

			// Server sets fly_mode (client only requests it)
			e->setProp("fly_mode", p.fly);

			e->velocity.x = p.desiredVel.x;
			e->velocity.z = p.desiredVel.z;

			// All entities face their movement direction (unified for RPG, RTS, AI)
			if (std::abs(p.desiredVel.x) > 0.01f || std::abs(p.desiredVel.z) > 0.01f) {
				e->yaw = glm::degrees(std::atan2(p.desiredVel.z, p.desiredVel.x));
			}
			e->pitch = p.lookPitch;

			if (p.fly) {
				e->velocity.y = p.desiredVel.y;
			} else if (p.jump && e->onGround) {
				e->velocity.y = p.jumpVelocity;
				e->onGround = false;
			}
			break;
		}

		case ActionProposal::BreakBlock: {
			Entity* actor = m_world->entities.get(p.actorId);
			auto& bp = p.blockPos;
			ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
			Chunk* c = m_world->getChunk(cp);
			if (!c) break;

			BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
			if (bid == BLOCK_AIR) break;
			const BlockDef& bdef = m_world->blocks.get(bid);

			if (actor) {
				float dist = glm::length(glm::vec3(bp) - actor->position);
				if (dist > 8.0f) break;
			}

			m_world->removeBlockState(bp.x, bp.y, bp.z);

			if (actor && actor->inventory) {
				std::string dropType = bdef.drop.empty() ? bdef.string_id : bdef.drop;
				if (!dropType.empty() && dropType != BlockType::Air) {
					// Spawn item entity at block center and launch toward the breaker
					glm::vec3 dropPos = glm::vec3(bp) + glm::vec3(0.5f, 0.5f, 0.5f);
					EntityId itemId = m_world->entities.spawn(EntityType::ItemEntity, dropPos,
						{{Prop::ItemType, dropType}, {Prop::Count, 1}, {Prop::Age, 0.0f}});
					// Item floats at block center. Client initiates pickup when in range.
				}
			}

			c->set(((bp.x % 16) + 16) % 16, ((bp.y % 16) + 16) % 16,
			       ((bp.z % 16) + 16) % 16, BLOCK_AIR);
			if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(bp, BLOCK_AIR, 0);
			// Sync inventory if actor broke a block and gained items
			if (actor && actor->inventory && m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::PlaceBlock: {
			Entity* actor = m_world->entities.get(p.actorId);
			auto& pp = p.blockPos;

			if (m_world->getBlock(pp.x, pp.y, pp.z) != BLOCK_AIR) break;

			if (actor) {
				auto& fp = actor->position;
				float hw = (actor->def().collision_box_max.x - actor->def().collision_box_min.x) * 0.5f;
				float ph = actor->def().collision_box_max.y;
				bool inside = pp.x >= (int)std::floor(fp.x - hw) &&
				              pp.x <= (int)std::floor(fp.x + hw) &&
				              pp.z >= (int)std::floor(fp.z - hw) &&
				              pp.z <= (int)std::floor(fp.z + hw) &&
				              pp.y >= (int)std::floor(fp.y) &&
				              pp.y <= (int)std::floor(fp.y + ph);
				if (inside) break;
				float dist = glm::length(glm::vec3(pp) - fp);
				if (dist > 8.0f) break;
			}

			if (actor && actor->inventory && !actor->inventory->has(p.blockType)) break;

			const BlockDef* placedDef = m_world->blocks.find(p.blockType);
			if (!placedDef) break;

			ChunkPos cp = worldToChunk(pp.x, pp.y, pp.z);
			Chunk* c = m_world->getChunk(cp);
			if (!c) break;

			// Door-specific: validate placement and auto-detect hinge
			uint8_t placeP2 = 0;
			if (placedDef->mesh_type == MeshType::Door) {
				auto isSolidCube = [&](int nx, int ny, int nz) {
					const BlockDef& nd = m_world->blocks.get(m_world->getBlock(nx, ny, nz));
					return nd.solid && nd.mesh_type == MeshType::Cube;
				};
				auto isDoorBlock = [&](int nx, int ny, int nz) {
					const std::string& s = m_world->blocks.get(m_world->getBlock(nx, ny, nz)).string_id;
					return s == BlockType::Door || s == BlockType::DoorOpen;
				};
				bool wXm = isSolidCube(pp.x-1, pp.y, pp.z);
				bool wXp = isSolidCube(pp.x+1, pp.y, pp.z);
				bool wZm = isSolidCube(pp.x, pp.y, pp.z-1);
				bool wZp = isSolidCube(pp.x, pp.y, pp.z+1);
				// Require at least one solid-cube wall to hinge against
				if (!wXm && !wXp && !wZm && !wZp) break;
				// Mirror hinge when placed next to an existing door (double-door)
				if      (isDoorBlock(pp.x-1, pp.y, pp.z)) placeP2 = 0x4; // door on left → right hinge
				else if (isDoorBlock(pp.x+1, pp.y, pp.z)) placeP2 = 0;   // door on right → left hinge
				else if (isDoorBlock(pp.x, pp.y, pp.z-1)) placeP2 = 0x4;
				else if (isDoorBlock(pp.x, pp.y, pp.z+1)) placeP2 = 0;
				// Single door: hinge on the side that has a solid wall
				else if (!wXm && wXp)  placeP2 = 0x4; // only right wall → right hinge
				else if (wXm && !wXp)  placeP2 = 0;   // only left wall → left hinge
				else if (!wZm && wZp)  placeP2 = 0x4;
				else if (wZm && !wZp)  placeP2 = 0;
				// both/neither: keep default left hinge
			}

			BlockId placedBid = m_world->blocks.getId(p.blockType);
			c->set(((pp.x % 16) + 16) % 16, ((pp.y % 16) + 16) % 16,
			       ((pp.z % 16) + 16) % 16, placedBid, placeP2);
			if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pp, placedBid, placeP2);

			if (placedDef->behavior == BlockBehavior::Active)
				m_world->setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);

			if (actor && actor->inventory)
				actor->inventory->remove(p.blockType, 1);
			// Sync inventory after block place
			if (actor && actor->inventory && m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::InteractBlock: {
			auto& bp = p.blockPos;
			Entity* actor = m_world->entities.get(p.actorId);
			if (actor && glm::length(glm::vec3(bp) - actor->position) > 6.0f) break;

			BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = m_world->blocks.get(bid);
			bool isDoor     = (bdef.string_id == BlockType::Door);
			bool isDoorOpen = (bdef.string_id == BlockType::DoorOpen);
			if (!isDoor && !isDoorOpen) break;

			BlockId closedId = m_world->blocks.getId(BlockType::Door);
			BlockId openId   = m_world->blocks.getId(BlockType::DoorOpen);
			BlockId newId    = isDoor ? openId : closedId;

			// Helper: toggle block ID while preserving existing param2 (hinge, rotation, etc.)
			auto setBlock = [&](int x, int y, int z, BlockId id) {
				ChunkPos cp = worldToChunk(x, y, z);
				Chunk* c = m_world->getChunk(cp);
				if (!c) return;
				int lx = ((x%16)+16)%16, ly = ((y%16)+16)%16, lz = ((z%16)+16)%16;
				uint8_t p2 = c->getParam2(lx, ly, lz);
				c->set(lx, ly, lz, id, p2);
				glm::ivec3 pos{x, y, z};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pos, id, p2);
			};

			// Toggle clicked block
			setBlock(bp.x, bp.y, bp.z, newId);

			// Scan upward/downward for connected door blocks (multi-block tall door)
			for (int dy = 1; dy <= 8; dy++) {
				BlockId above = m_world->getBlock(bp.x, bp.y+dy, bp.z);
				if (above == closedId || above == openId) setBlock(bp.x, bp.y+dy, bp.z, newId);
				else break;
			}
			for (int dy = 1; dy <= 8; dy++) {
				BlockId below = m_world->getBlock(bp.x, bp.y-dy, bp.z);
				if (below == closedId || below == openId) setBlock(bp.x, bp.y-dy, bp.z, newId);
				else break;
			}
			break;
		}

		case ActionProposal::IgniteTNT: {
			auto& bp = p.blockPos;
			BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = m_world->blocks.get(bid);
			if (bdef.string_id != BlockType::TNT) break;
			auto* tntState = m_world->getBlockState(bp.x, bp.y, bp.z);
			if (!tntState) {
				m_world->setBlockState(bp.x, bp.y, bp.z,
					{{Prop::Lit, 1}, {Prop::FuseTicks, 60}});
			} else if ((*tntState)[Prop::Lit] == 0) {
				(*tntState)[Prop::Lit] = 1;
				(*tntState)[Prop::FuseTicks] = 60;
			}
			break;
		}

		case ActionProposal::DropItem: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor) break;
			if (p.blockType.empty()) break;
			if (!actor->def().isLiving()) break;
			int count = std::clamp(p.itemCount, 1, 64);

			// Deduct from inventory
			if (actor->inventory && actor->inventory->has(p.blockType)) {
				actor->inventory->remove(p.blockType, count);
				actor->inventory->autoPopulateHotbar();
				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			}

			// Spawn item entity and toss toward cursor direction
			glm::vec3 tossDir = p.desiredVel;
			float tossLen = glm::length(tossDir);
			if (tossLen < 0.1f) {
				// Fallback: use actor's yaw direction
				float yaw = glm::radians(actor->yaw);
				tossDir = glm::vec3(std::cos(yaw), 0.5f, std::sin(yaw)) * 5.0f;
			}
			// Clamp toss speed (anti-cheat: max ~8 blocks/sec)
			if (tossLen > 10.0f) tossDir = glm::normalize(tossDir) * 10.0f;
			glm::vec3 xzDir = glm::vec3(tossDir.x, 0, tossDir.z);
			float xzLen = glm::length(xzDir);
			glm::vec3 fwd = xzLen > 0.01f ? xzDir / xzLen
			                              : glm::vec3(std::cos(glm::radians(actor->yaw)), 0,
			                                          std::sin(glm::radians(actor->yaw)));
			// Spawn 1.5 blocks ahead (outside pickup range) at eye height
			glm::vec3 dropPos = actor->position + fwd * 1.5f + glm::vec3(0, 1.2f, 0);
			EntityId itemId = m_world->entities.spawn(EntityType::ItemEntity, dropPos,
				{{Prop::ItemType, p.blockType}, {Prop::Count, count}, {Prop::Age, 0.0f}});
			Entity* ie = m_world->entities.get(itemId);
			if (ie) ie->velocity = tossDir;
			break;
		}

		case ActionProposal::Attack: {
			Entity* actor = m_world->entities.get(p.actorId);
			Entity* target = m_world->entities.get(p.targetEntity);
			if (!actor || !target || target->removed) break;
			if (!target->def().isLiving()) break;

			float dist = glm::length(target->position - actor->position);
			if (dist > 6.0f) break; // max attack range

			int dmg = std::max((int)p.damage, 1);
			int hp = target->hp();
			target->setHp(std::max(hp - dmg, 0));

			// Knockback
			glm::vec3 kb = (dist > 0.1f)
				? glm::normalize(target->position - actor->position) : glm::vec3(0, 0, 1);
			target->velocity += kb * 4.0f + glm::vec3(0, 3.0f, 0);

			// Death: drop loot and remove
			if (target->hp() <= 0) {
				target->removed = true;

				// Drop 1–2 meat for animals (not players, not villagers)
				if (target->def().category == Category::Animal) {
					int count = 1 + (rand() % 2); // 1 or 2 pieces
					// Scatter loot slightly above death position
					glm::vec3 lootPos = target->position + glm::vec3(0, 0.3f, 0);
					glm::vec3 fwd = (dist > 0.1f)
						? glm::normalize(target->position - actor->position) : glm::vec3(0,0,1);
					EntityId lootId = m_world->entities.spawn(EntityType::ItemEntity, lootPos,
						{{Prop::ItemType, std::string("base:meat")},
						 {Prop::Count, count},
						 {Prop::Age, 0.0f}});
					Entity* le = m_world->entities.get(lootId);
					if (le) {
						// Toss loot slightly away from attacker with upward arc
						le->velocity = fwd * 2.5f + glm::vec3(0, 4.5f, 0);
					}
				}
			}
			break;
		}

		case ActionProposal::UseItem: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor || !actor->inventory) break;

			int slot = std::clamp(p.slotIndex, 0, Inventory::HOTBAR_SLOTS - 1);
			const std::string& itemId = actor->inventory->hotbar(slot);
			if (itemId.empty() || !actor->inventory->has(itemId)) break;

			// Consume 1 item and apply heal (amount from client's artifact lookup)
			actor->inventory->remove(itemId, 1);
			int healAmt = (p.damage > 0) ? (int)p.damage : 4; // p.damage carries effect_amount
			int hp = actor->hp();
			if (hp < actor->def().max_hp)
				actor->setHp(std::min(hp + healAmt, actor->def().max_hp));
			actor->inventory->autoPopulateHotbar();
			if (m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::EquipItem: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor || !actor->inventory) break;

			int slot = std::clamp(p.slotIndex, 0, Inventory::HOTBAR_SLOTS - 1);
			const std::string& itemId = actor->inventory->hotbar(slot);
			if (itemId.empty() || !actor->inventory->has(itemId)) break;

			// Parse equip slot from blockType field (sent by client from artifact)
			WearSlot ws;
			if (!wearSlotFromString(p.blockType, ws)) {
				printf("[Server] EquipItem FAILED: unknown slot '%s' for item '%s'\n",
					p.blockType.c_str(), itemId.c_str());
				break;
			}

			printf("[Server] EquipItem: '%s' → slot %d ('%s')\n",
				itemId.c_str(), (int)ws, p.blockType.c_str());
			actor->inventory->equip(ws, itemId);
			actor->inventory->autoPopulateHotbar();
			if (m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::StoreItem: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor || !actor->inventory) break;
			if (!actor->def().isLiving()) break;

			// Look up the chest entity by targetEntity ID.
			Entity* chestEnt = m_world->entities.get(p.targetEntity);
			if (!chestEnt || chestEnt->def().category != Category::Chest) {
				printf("[Server] StoreItem DENIED (entity %u): invalid chest entity %u\n",
					actor->id(), p.targetEntity);
				break;
			}

			// Get the block position from the chest entity's position.
			glm::ivec3 cp = {(int)std::floor(chestEnt->position.x),
			                 (int)std::floor(chestEnt->position.y),
			                 (int)std::floor(chestEnt->position.z)};

			// Validate there is actually a chest block there.
			BlockId bid = m_world->getBlock(cp.x, cp.y, cp.z);
			const BlockDef& bdef = m_world->blocks.get(bid);
			if (bdef.string_id != BlockType::Chest) {
				printf("[Server] StoreItem DENIED (entity %u): no chest block at (%d,%d,%d), found '%s'\n",
					actor->id(), cp.x, cp.y, cp.z, bdef.string_id.c_str());
				break;
			}

			if (!chestEnt->inventory) break;

			// Transfer all items from actor to chest entity inventory.
			int totalTransferred = 0;
			for (auto& [itemId, count] : actor->inventory->items()) {
				chestEnt->inventory->add(itemId, count);
				totalTransferred += count;
			}
			actor->inventory->clear();
			actor->inventory->autoPopulateHotbar();

			// Keep block inventory map in sync for world save.
			uint64_t posKey = ((uint64_t)(uint32_t)cp.x)
			                | ((uint64_t)(uint32_t)cp.y << 21)
			                | ((uint64_t)(uint32_t)cp.z << 42);
			m_blockInventories[posKey] = *chestEnt->inventory;

			printf("[Server] StoreItem OK: entity %u '%s' deposited %d items into chest entity %u at (%d,%d,%d)\n",
				actor->id(), actor->typeId().c_str(), totalTransferred,
				chestEnt->id(), cp.x, cp.y, cp.z);

			if (m_callbacks.onInventoryChange) {
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				m_callbacks.onInventoryChange(chestEnt->id(), *chestEnt->inventory);
			}
			break;
		}
		case ActionProposal::GrowCrop:
			break;
		case ActionProposal::PickupItem: {
			Entity* actor = m_world->entities.get(p.actorId);
			Entity* item = m_world->entities.get(p.targetEntity);
			if (!actor || !item) break;
			if (!actor->inventory) break;
			if (item->typeId() != EntityType::ItemEntity) break;
			if (item->removed) break;

			// Server validates max pickup distance (anti-cheat ceiling)
			float dist = glm::length(item->position - actor->position);
			float range = std::min(m_wgc.pickupRange, 5.0f); // server cap: 5 blocks
			if (dist > range) break; // Denied — too far (client shows nothing; item stays)

			// Approved — collect item
			std::string itemType = item->getProp<std::string>(Prop::ItemType);
			int count = item->getProp<int>(Prop::Count, 1);
			actor->inventory->add(itemType, count);
			item->removed = true;

			actor->inventory->autoPopulateHotbar();
			if (m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}
		} // switch
	} // for
} // resolveActions

} // namespace modcraft

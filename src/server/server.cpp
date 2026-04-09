#include "server/server.h"
#include "shared/material_values.h"
#include <cmath>

namespace modcraft {

void GameServer::resolveActions(float dt) {
	auto proposals = m_world->proposals.drain();

	// Per-tick deduplication: each entity gets at most one nudge-back per tick.
	// Prevents inventory resend spam when a client sends many invalid actions.
	std::unordered_set<EntityId> nudgedThisTick;

	for (auto& p : proposals) {
		switch (p.type) {

		case ActionProposal::Move: {
			Entity* e = m_world->entities.get(p.actorId);
			if (!e) break;

			// Accept client-reported position if within tolerance.
			// Client tracks its own position; server only corrects on large errors.
			// Server still runs collision after this, so wall-phasing is impossible.
			constexpr float CLIENT_POS_TOLERANCE = 8.0f;
			if (p.hasClientPos) {
				float dist = glm::length(p.clientPos - e->position);
				if (dist < CLIENT_POS_TOLERANCE) {
					e->position = p.clientPos;
					// Client already ran moveAndCollide — skip server physics this tick
					// to prevent double gravity/collision application.
					e->skipPhysics = true;
				}
			}

			// Clamp to entity's max speed (anti-cheat).
			// Sprint allows 2.5x, admin/fly is uncapped. Tolerance: 3.5x walk.
			float maxSpeed = e->def().walk_speed;
			if (maxSpeed > 0) {
				float speedCap = maxSpeed * (p.sprint ? 3.5f : 1.5f);
				float len = glm::length(glm::vec2(p.desiredVel.x, p.desiredVel.z));
				if (len > speedCap) {
					float scale = speedCap / len;
					p.desiredVel.x *= scale;
					p.desiredVel.z *= scale;
				}
			}

			e->setProp("fly_mode", p.fly);

			e->velocity.x = p.desiredVel.x;
			e->velocity.z = p.desiredVel.z;

			// Derive move target for client-side prediction (10-block lookahead)
			float hLen = std::sqrt(p.desiredVel.x * p.desiredVel.x + p.desiredVel.z * p.desiredVel.z);
			if (hLen > 0.01f) {
				glm::vec3 dir = {p.desiredVel.x / hLen, 0, p.desiredVel.z / hLen};
				e->moveTarget = e->position + dir * 10.0f;
				e->moveSpeed = hLen;
				e->yaw = glm::degrees(std::atan2(p.desiredVel.z, p.desiredVel.x));
			} else {
				e->moveTarget = e->position;
				e->moveSpeed = 0;
			}
			e->pitch = p.lookPitch;

			if (p.hasClientPos) {
				// Client ran moveAndCollide — trust its Y velocity (gravity, jump, etc.)
				e->velocity.y = p.desiredVel.y;
			} else if (p.fly) {
				e->velocity.y = p.desiredVel.y;
			} else if (p.jump && e->onGround) {
				e->velocity.y = p.jumpVelocity;
				e->onGround = false;
			}
			break;
		}

		case ActionProposal::Relocate: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor) break;

			// Nudge: re-send current inventory so client corrects any optimistic state.
			// Deduplicated per tick — at most one resend per entity regardless of spam.
			auto nudgeR = [&](ActionRejectCode code) {
				printf("[Server] Relocate rejected entity=%u code=%u\n",
				       p.actorId, (uint32_t)code);
				if (nudgedThisTick.count(p.actorId)) return;
				nudgedThisTick.insert(p.actorId);
				if (actor->inventory && m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			};

			if (p.relocateTo.kind == Container::Kind::Ground) {
				// Drop item from actor inventory as a spawned item entity
				if (!actor->inventory) break;
				std::string dropType = p.itemId;
				if (dropType.empty()) break;
				if (!actor->inventory->has(dropType)) { nudgeR(ActionRejectCode::ItemNotInInventory); break; }

				int count = std::clamp(p.itemCount, 1, 64);
				actor->inventory->remove(dropType, count);
				actor->inventory->autoPopulateHotbar();
				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);

				// Spawn item entity and toss
				glm::vec3 tossDir = p.desiredVel;
				float tossLen = glm::length(tossDir);
				if (tossLen < 0.1f) {
					float yaw = glm::radians(actor->yaw);
					tossDir = glm::vec3(std::cos(yaw), 0.5f, std::sin(yaw)) * 5.0f;
				}
				if (tossLen > 10.0f) tossDir = glm::normalize(tossDir) * 10.0f;
				glm::vec3 xzDir = glm::vec3(tossDir.x, 0, tossDir.z);
				float xzLen = glm::length(xzDir);
				glm::vec3 fwd = xzLen > 0.01f ? xzDir / xzLen
				                              : glm::vec3(std::cos(glm::radians(actor->yaw)), 0,
				                                          std::sin(glm::radians(actor->yaw)));
				glm::vec3 dropPos = actor->position + fwd * 1.5f + glm::vec3(0, 1.2f, 0);
				EntityId itemEntityId = m_world->entities.spawn(EntityType::ItemEntity, dropPos,
					{{Prop::ItemType, dropType}, {Prop::Count, count}, {Prop::Age, 0.0f}});
				Entity* ie = m_world->entities.get(itemEntityId);
				if (ie) ie->velocity = tossDir;
				break;
			}

			if (!p.equipSlot.empty()) {
				// Equip item to wear slot
				if (!actor->inventory) break;
				WearSlot ws;
				if (!wearSlotFromString(p.equipSlot, ws)) {
					printf("[Server] Relocate/equip FAILED: unknown slot '%s'\n", p.equipSlot.c_str());
					break;
				}
				std::string equipId = p.itemId;
				if (equipId.empty()) break;
				if (!actor->inventory->has(equipId)) break;
				printf("[Server] Relocate/equip: '%s' → slot '%s'\n", equipId.c_str(), p.equipSlot.c_str());
				actor->inventory->equip(ws, equipId);
				actor->inventory->autoPopulateHotbar();
				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				break;
			}

			if (p.relocateTo.kind == Container::Kind::Block) {
				// Store: transfer all items from actor into a chest block's inventory.
				// Actor must be within 2 blocks.
				if (!actor->inventory) break;
				glm::ivec3 cp = p.relocateTo.pos;
				BlockId bid = m_world->getBlock(cp.x, cp.y, cp.z);
				const BlockDef& bdef = m_world->blocks.get(bid);
				if (bdef.string_id != BlockType::Chest) break;

				float storeDist = glm::length(glm::vec3(cp) + 0.5f - actor->position);
				if (storeDist > 2.5f) {
					printf("[Server] StoreItem DENIED (entity %u): %.1f blocks from chest (max 2.5)\n",
						actor->id(), storeDist);
					break;
				}

				uint64_t posKey = packBlockPos(cp.x, cp.y, cp.z);
				auto& chestInv = m_blockInventories[posKey];

				int totalTransferred = 0;
				for (auto& [itemIdStr, count] : actor->inventory->items()) {
					chestInv.add(itemIdStr, count);
					totalTransferred += count;
				}
				actor->inventory->clear();
				actor->inventory->autoPopulateHotbar();

				printf("[Server] Relocate/store: entity %u deposited %d items into chest at (%d,%d,%d)\n",
					actor->id(), totalTransferred, cp.x, cp.y, cp.z);

				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				break;
			}

			if (p.relocateFrom.kind == Container::Kind::Entity) {
				// Pickup from entity (item entity or chest)
				Entity* src = m_world->entities.get(p.relocateFrom.entityId);
				if (!src || src->removed) { nudgeR(ActionRejectCode::SourceEntityGone); break; }
				if (!actor->inventory) break;

				if (src->typeId() == EntityType::ItemEntity) {
					// Item entity pickup: validate range using the actor's pickup_range
					float dist = glm::length(src->position - actor->position);
					float maxRange = actor->def().pickup_range;
					if (maxRange <= 0) maxRange = 1.5f;
					if (dist > maxRange) { nudgeR(ActionRejectCode::PickupOutOfRange); break; }

					std::string itemType = src->getProp<std::string>(Prop::ItemType);
					int count = src->getProp<int>(Prop::Count, 1);
					actor->inventory->add(itemType, count);
					src->removed = true;

					actor->inventory->autoPopulateHotbar();
					if (m_callbacks.onInventoryChange)
						m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				}
				break;
			}
			break;
		}

		case ActionProposal::Convert: {
			Entity* actor = m_world->entities.get(p.actorId);
			if (!actor) break;

			// Nudge: re-send current inventory so client corrects any optimistic state.
			// Deduplicated per tick — at most one resend per entity regardless of spam.
			auto nudge = [&](ActionRejectCode code) {
				printf("[Server] Convert rejected entity=%u from=%s to=%s code=%u\n",
				       p.actorId, p.fromItem.c_str(), p.toItem.c_str(), (uint32_t)code);
				if (nudgedThisTick.count(p.actorId)) return;
				nudgedThisTick.insert(p.actorId);
				if (actor->inventory && m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			};

			// Act on another entity's HP or inventory (e.g. attack: destroy target's HP)
			if (p.convertFrom.kind == Container::Kind::Entity) {
				Entity* target = m_world->entities.get(p.convertFrom.entityId);
				if (!target || target->removed) break;

				if (p.fromItem == "hp") {
					if (!target->def().isLiving()) break;

					int dmg = std::max(p.fromCount, 1);
					int hp = target->hp();
					target->setHp(std::max(hp - dmg, 0));

					// Knockback
					glm::vec3 diff = target->position - actor->position;
					float dist = glm::length(diff);
					glm::vec3 kb = (dist > 0.1f) ? glm::normalize(diff) : glm::vec3(0, 0, 1);
					target->velocity += kb * 4.0f + glm::vec3(0, 3.0f, 0);

					// Death
					if (target->hp() <= 0) {
						target->removed = true;

						// Drop loot for animals
						if (target->def().category == Category::Animal) {
							int count = 1 + (rand() % 2);
							glm::vec3 lootPos = target->position + glm::vec3(0, 0.3f, 0);
							glm::vec3 fwd = (dist > 0.1f) ? glm::normalize(diff) : glm::vec3(0, 0, 1);
							EntityId lootId = m_world->entities.spawn(EntityType::ItemEntity, lootPos,
								{{Prop::ItemType, std::string("base:meat")},
								 {Prop::Count, count},
								 {Prop::Age, 0.0f}});
							Entity* le = m_world->entities.get(lootId);
							if (le) le->velocity = fwd * 2.5f + glm::vec3(0, 4.5f, 0);
						}
					}
				} else {
					// Destroy specific item from target's inventory
					if (target->inventory && target->inventory->has(p.fromItem))
						target->inventory->remove(p.fromItem, p.fromCount);
				}
				break;
			}

			// Value conservation check
			float inVal  = getMaterialValue(p.fromItem) * (float)p.fromCount;
			float outVal = getMaterialValue(p.toItem)   * (float)p.toCount;
			if (outVal > inVal + 0.001f) {
				nudge(ActionRejectCode::ValueConservationViolated);
				break;
			}

			const bool fromBlock = (p.convertFrom.kind == Container::Kind::Block);
			const bool intoBlock = (p.convertInto.kind == Container::Kind::Block);

			// Pre-validate placement target BEFORE consuming source (prevents item loss on race)
			if (intoBlock) {
				auto& pp = p.convertInto.pos;
				if (m_world->getBlock(pp.x, pp.y, pp.z) != BLOCK_AIR)  { nudge(ActionRejectCode::PlacementTargetOccupied); break; }
				if (!m_world->blocks.find(p.toItem))                    { nudge(ActionRejectCode::UnknownBlockType);        break; }
				if (!m_world->getChunk(worldToChunk(pp.x, pp.y, pp.z))){ nudge(ActionRejectCode::ChunkNotLoaded);          break; }
			}

			// Consume source
			if (fromBlock) {
				// Source is a world block
				auto& bp = p.convertFrom.pos;
				BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
				if (bid == BLOCK_AIR) { nudge(ActionRejectCode::SourceBlockGone); break; }
				const BlockDef& bdef = m_world->blocks.get(bid);
				// Anti-cheat: client must send matching fromItem
				if (!p.fromItem.empty() && bdef.string_id != p.fromItem) { nudge(ActionRejectCode::SourceBlockTypeMismatch); break; }

				m_world->removeBlockState(bp.x, bp.y, bp.z);
				ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
				Chunk* c = m_world->getChunk(cp);
				if (!c) break;
				c->set(((bp.x % 16) + 16) % 16, ((bp.y % 16) + 16) % 16,
				       ((bp.z % 16) + 16) % 16, BLOCK_AIR);
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(bp, bid, BLOCK_AIR, 0);
			} else if (p.fromItem == "hp") {
				// Consume HP
				int hp = actor->hp();
				actor->setHp(std::max(hp - p.fromCount, 0));
			} else {
				// Consume from actor's inventory (Self)
				if (!actor->inventory) break;
				if (!actor->inventory->has(p.fromItem)) { nudge(ActionRejectCode::ItemNotInInventory); break; }
				actor->inventory->remove(p.fromItem, p.fromCount);
			}

			// Produce output
			if (intoBlock) {
				// Place block (pre-validated above — target is guaranteed clear)
				auto& pp = p.convertInto.pos;
				const BlockDef* placedDef = m_world->blocks.find(p.toItem);
				ChunkPos cp = worldToChunk(pp.x, pp.y, pp.z);
				Chunk* c = m_world->getChunk(cp);
				if (!c || !placedDef) break;  // chunk unloaded between pre-check and now — rare

				// Door-specific: auto-detect hinge
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
					if (!wXm && !wXp && !wZm && !wZp) break;
					if      (isDoorBlock(pp.x-1, pp.y, pp.z)) placeP2 = 0x4;
					else if (isDoorBlock(pp.x+1, pp.y, pp.z)) placeP2 = 0;
					else if (isDoorBlock(pp.x, pp.y, pp.z-1)) placeP2 = 0x4;
					else if (isDoorBlock(pp.x, pp.y, pp.z+1)) placeP2 = 0;
					else if (!wXm && wXp)  placeP2 = 0x4;
					else if (wXm && !wXp)  placeP2 = 0;
					else if (!wZm && wZp)  placeP2 = 0x4;
					else if (wZm && !wZp)  placeP2 = 0;
				}

				// Check actor body doesn't overlap placement
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
				}

				BlockId placedBid = m_world->blocks.getId(p.toItem);
				c->set(((pp.x % 16) + 16) % 16, ((pp.y % 16) + 16) % 16,
				       ((pp.z % 16) + 16) % 16, placedBid, placeP2);
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pp, BLOCK_AIR, placedBid, placeP2);

				if (placedDef->behavior == BlockBehavior::Active)
					m_world->setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);
			} else if (p.toItem == "hp") {
				// Heal actor
				int hp = actor->hp();
				if (hp < actor->def().max_hp)
					actor->setHp(std::min(hp + p.toCount, actor->def().max_hp));
			} else if (!p.toItem.empty()) {
				if (p.convertInto.kind == Container::Kind::Ground) {
					// Spawn as item entity near actor or at the source block position
					glm::vec3 spawnPos = fromBlock
						? (glm::vec3(p.convertFrom.pos) + glm::vec3(0.5f, 0.5f, 0.5f))
						: (actor->position + glm::vec3(0, 0.3f, 0));
					m_world->entities.spawn(EntityType::ItemEntity, spawnPos,
						{{Prop::ItemType, p.toItem}, {Prop::Count, p.toCount}, {Prop::Age, 0.0f}});
				} else {
					// Default: add directly to actor's inventory (Self)
					if (actor->inventory) {
						actor->inventory->add(p.toItem, p.toCount);
						actor->inventory->autoPopulateHotbar();
					}
				}
			}

			// Sync inventory
			if (actor->inventory && m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::Interact: {
			auto& bp = p.blockPos;

			BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = m_world->blocks.get(bid);

			// TNT ignition
			if (bdef.string_id == BlockType::TNT) {
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

			// Door toggle
			bool isDoor     = (bdef.string_id == BlockType::Door);
			bool isDoorOpen = (bdef.string_id == BlockType::DoorOpen);
			if (!isDoor && !isDoorOpen) break;

			BlockId closedId = m_world->blocks.getId(BlockType::Door);
			BlockId openId   = m_world->blocks.getId(BlockType::DoorOpen);
			BlockId newId    = isDoor ? openId : closedId;

			auto setBlock = [&](int x, int y, int z, BlockId id) {
				ChunkPos cp = worldToChunk(x, y, z);
				Chunk* c = m_world->getChunk(cp);
				if (!c) return;
				int lx = ((x%16)+16)%16, ly = ((y%16)+16)%16, lz = ((z%16)+16)%16;
				uint8_t p2 = c->getParam2(lx, ly, lz);
				BlockId oldId = c->get(lx, ly, lz);  // read BEFORE set
				c->set(lx, ly, lz, id, p2);
				glm::ivec3 pos{x, y, z};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pos, oldId, id, p2);
			};

			setBlock(bp.x, bp.y, bp.z, newId);

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

		// default: unknown type — ignore
		default: break;

		} // switch
	} // for
} // resolveActions

} // namespace modcraft

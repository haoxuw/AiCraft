#include "server/server.h"
#include "shared/material_values.h"
#include "shared/move_stuck_log.h"
#include <cmath>

namespace civcraft {

void GameServer::resolveActions(float dt) {
	auto proposals = m_world->proposals.drain();
	m_actionStats.resolved += (int)proposals.size();

	// Advance per-entity reject cooldowns once per tick; entries expire to 0
	// naturally and are cleaned out so the map doesn't grow unbounded.
	for (auto it = m_clientPosRejectCooldown.begin(); it != m_clientPosRejectCooldown.end();) {
		if (--it->second <= 0) it = m_clientPosRejectCooldown.erase(it);
		else                    ++it;
	}

	// Per-tick deduplication: each entity gets at most one nudge-back per tick.
	// Prevents inventory resend spam when a client sends many invalid actions.
	std::unordered_set<EntityId> nudgedThisTick;

	for (auto& p : proposals) {
		switch (p.type) {

		case ActionProposal::Move: {
			Entity* e = m_world->entities.get(p.actorId);
			if (!e) {
				char buf[96];
				std::snprintf(buf, sizeof(buf),
					"entity gone between receive and resolve (intent=(%.2f,%.2f))",
					p.desiredVel.x, p.desiredVel.z);
				logMoveReject(p.actorId, "entity-gone-at-resolve", buf);
				break;
			}

			// Accept client-reported position if within tolerance AND it doesn't
			// overlap any solid block. Client runs moveAndCollide locally; the
			// server trusts the result only after verifying no wall-phasing.
			// Rejected clientPos falls through to server-authoritative physics.
			//
			// Cooldown: after a rejection we skip subsequent clientPos checks
			// for this entity for kRejectCooldownTicks ticks. This swallows
			// the stale Move actions already in the TCP pipe from before the
			// client received the S_BLOCK update that would have taught it
			// about the new block. Suppressed proposals fall through to
			// server-authoritative physics silently — the first reject already
			// logged, additional logs would just be noise.
			constexpr float CLIENT_POS_TOLERANCE = 8.0f;
			constexpr int   kRejectCooldownTicks = 10;
			bool inCooldown = m_clientPosRejectCooldown.count(p.actorId) > 0;
			if (p.hasClientPos && !inCooldown) {
				float dist = glm::length(p.clientPos - e->position);
				if (dist < CLIENT_POS_TOLERANCE) {
					const auto& def = e->def();
					MoveParams mp = makeMoveParams(
						def.collision_box_min, def.collision_box_max,
						def.gravity_scale, def.isLiving(), p.fly);
					BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
						const auto& bd = m_world->blocks.get(
							m_world->getBlock(x, y, z));
						return bd.solid ? bd.collision_height : 0.0f;
					};
					if (!isPositionBlocked(solidFn, p.clientPos,
					                       mp.halfWidth, mp.height)) {
						e->position = p.clientPos;
						// Client already ran moveAndCollide — skip server physics
						// this tick to prevent double gravity/collision.
						e->skipPhysics = true;
					} else {
						// First rejection in this burst: log, then open the
						// cooldown window. In-flight stale proposals that
						// arrive over the next ~167ms hit the cooldown branch
						// above and are silently server-authored.
						m_clientPosRejectCooldown[p.actorId] = kRejectCooldownTicks;
						char detail[160];
						std::snprintf(detail, sizeof(detail),
							"clientPos=(%.2f,%.2f,%.2f) overlaps solid block — rejected, cooldown %d ticks",
							p.clientPos.x, p.clientPos.y, p.clientPos.z,
							kRejectCooldownTicks);
						logMoveReject(p.actorId, "client-pos-in-wall", detail);
					}
				}
			}
			// During cooldown we must also ignore p.hasClientPos for the Y
			// velocity branch below, otherwise server would trust a stale
			// post-physics Y and desync vertically.
			if (inCooldown) p.hasClientPos = false;

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

			// ── DEBUG: server-side Move receipt probe (10-min cooldown) ──
			// (noisy per-entity startup diagnostic removed — re-enable if
			// you need to debug client-vs-server physics routing.)

			// Derive move target for client-side prediction (10-block lookahead)
			float hLen = std::sqrt(p.desiredVel.x * p.desiredVel.x + p.desiredVel.z * p.desiredVel.z);
			if (hLen > 0.01f) {
				glm::vec3 dir = {p.desiredVel.x / hLen, 0, p.desiredVel.z / hLen};
				e->moveTarget = e->position + dir * 10.0f;
				e->moveSpeed = hLen;
				// yaw is smoothed per-tick in GameServer::tick from velocity.
			} else {
				e->moveTarget = e->position;
				e->moveSpeed = 0;
			}
			e->lookPitch = p.lookPitch;
			e->lookYaw   = p.lookYaw;

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
			//
			// The caller supplies a `why` string describing what was attempted
			// (item, source→dest, distance vs threshold). Without it, a
			// rejected Relocate is opaque: you only see the numeric code. Log
			// what we were trying to do so the user can diagnose without
			// reading server source for every code.
			auto nudgeR = [&](ActionRejectCode code, const std::string& why) {
				logActionReject("Relocate", p.actorId, rejectCodeName(code), why.c_str());
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
				if (!actor->inventory->has(dropType)) {
					nudgeR(ActionRejectCode::ItemNotInInventory,
						"drop: actor has 0× '" + dropType + "'");
					break;
				}

				int count = std::clamp(p.itemCount, 1, 64);
				actor->inventory->remove(dropType, count);
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
				EntityId itemEntityId = m_world->entities.spawn(ItemName::ItemEntity, dropPos,
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
				if (m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				break;
			}

			if (p.relocateTo.kind == Container::Kind::Entity) {
				// Store into another entity's inventory (chest, Creatures, etc.)
				Entity* target = m_world->entities.get(p.relocateTo.entityId);
				if (!target || target->removed) {
					char buf[128];
					snprintf(buf, sizeof(buf), "store: target entity=%u missing/removed",
						p.relocateTo.entityId);
					nudgeR(ActionRejectCode::TargetEntityGone, buf);
					break;
				}
				if (!target->inventory) {
					char buf[128];
					snprintf(buf, sizeof(buf), "store: target entity=%u type='%s' has no inventory",
						target->id(), target->typeId().c_str());
					nudgeR(ActionRejectCode::TargetHasNoInventory, buf);
					break;
				}
				if (!actor->inventory) {
					nudgeR(ActionRejectCode::ActorHasNoInventory, "store: actor has no inventory");
					break;
				}

				float dist = glm::length(target->position - actor->position);
				if (dist > m_wgc.storeRange) {
					char buf[192];
					snprintf(buf, sizeof(buf),
						"store: %.2f blocks from target=%u (max %.1f) actor@(%.1f,%.1f,%.1f) target@(%.1f,%.1f,%.1f)",
						dist, target->id(), m_wgc.storeRange,
						actor->position.x, actor->position.y, actor->position.z,
						target->position.x, target->position.y, target->position.z);
					// Out-of-range store is not a code-based reject today; log and drop.
					printf("[Server] Relocate rejected entity=%u (%s)\n", actor->id(), buf);
					break;
				}

				int totalTransferred = 0;
				if (!p.itemId.empty()) {
					// Transfer specific item × count
					int count = std::clamp(p.itemCount, 1, 64);
					if (!actor->inventory->has(p.itemId, count)) {
						char buf[160];
						snprintf(buf, sizeof(buf),
							"store: actor lacks %d× '%s' (has %d)", count,
							p.itemId.c_str(), actor->inventory->count(p.itemId));
						nudgeR(ActionRejectCode::ItemNotInInventory, buf);
						break;
					}
					actor->inventory->remove(p.itemId, count);
					target->inventory->add(p.itemId, count);
					totalTransferred = count;
				} else {
					// Transfer all items
					for (auto& [itemIdStr, count] : actor->inventory->items()) {
						target->inventory->add(itemIdStr, count);
						totalTransferred += count;
					}
					actor->inventory->clear();
				}

				printf("[Server] Relocate/store: entity %u deposited %d items into entity %u\n",
					actor->id(), totalTransferred, target->id());

				if (m_callbacks.onInventoryChange) {
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
					m_callbacks.onInventoryChange(target->id(), *target->inventory);
				}
				break;
			}

			if (p.relocateFrom.kind == Container::Kind::Entity) {
				Entity* src = m_world->entities.get(p.relocateFrom.entityId);
				if (!src || src->removed) {
					char buf[128];
					snprintf(buf, sizeof(buf),
						"take: source entity=%u missing/removed (likely item already picked up by someone else)",
						p.relocateFrom.entityId);
					nudgeR(ActionRejectCode::SourceEntityGone, buf);
					break;
				}
				if (!actor->inventory) {
					nudgeR(ActionRejectCode::ActorHasNoInventory, "take: actor has no inventory");
					break;
				}

				if (src->typeId() == ItemName::ItemEntity) {
					// Item entity pickup: validate range using the actor's pickup_range
					float dist = glm::length(src->position - actor->position);
					float maxRange = actor->def().pickup_range;
					if (maxRange <= 0) maxRange = 1.5f;
					if (dist > maxRange) {
						std::string itemType = src->getProp<std::string>(Prop::ItemType);
						int cnt = src->getProp<int>(Prop::Count, 1);
						char buf[256];
						snprintf(buf, sizeof(buf),
							"pickup: %d× '%s' (item entity=%u) %.2f blocks away (max %.2f) "
							"actor@(%.2f,%.2f,%.2f) item@(%.2f,%.2f,%.2f)",
							cnt, itemType.c_str(), src->id(), dist, maxRange,
							actor->position.x, actor->position.y, actor->position.z,
							src->position.x, src->position.y, src->position.z);
						nudgeR(ActionRejectCode::PickupOutOfRange, buf);
						break;
					}

					std::string itemType = src->getProp<std::string>(Prop::ItemType);
					int count = src->getProp<int>(Prop::Count, 1);
					if (!actor->inventory->canAccept(itemType, count,
					                                 actor->def().inventory_capacity)) {
						char buf[192];
						float cap = actor->def().inventory_capacity;
						snprintf(buf, sizeof(buf),
							"pickup: inventory can't accept %d× '%s' (cap=%.1f, used=%.1f)",
							count, itemType.c_str(), cap, actor->inventory->totalValue());
						nudgeR(ActionRejectCode::ItemNotInInventory, buf);
						break;
					}
					actor->inventory->add(itemType, count);
					src->removed = true;

					if (m_callbacks.onInventoryChange)
						m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				} else if (src->inventory) {
					// Take from another entity's inventory (chest, Creatures, etc.)
					float dist = glm::length(src->position - actor->position);
					if (dist > m_wgc.storeRange) {
						char buf[160];
						snprintf(buf, sizeof(buf),
							"take: %.2f blocks from source=%u (max %.1f)",
							dist, src->id(), m_wgc.storeRange);
						printf("[Server] Relocate rejected entity=%u (%s)\n", actor->id(), buf);
						break;
					}
					if (p.itemId.empty()) {
						nudgeR(ActionRejectCode::MissingItemId,
							"take: no itemId specified (cannot take 'all' from entity)");
						break;
					}
					int count = std::clamp(p.itemCount, 1, 64);
					if (!src->inventory->has(p.itemId, count)) {
						char buf[160];
						snprintf(buf, sizeof(buf),
							"take: source=%u has %d× '%s', asked for %d",
							src->id(), src->inventory->count(p.itemId), p.itemId.c_str(), count);
						nudgeR(ActionRejectCode::ItemNotInInventory, buf);
						break;
					}
					if (!actor->inventory->canAccept(p.itemId, count,
					                                  actor->def().inventory_capacity)) {
						char buf[160];
						snprintf(buf, sizeof(buf),
							"take: actor inventory can't accept %d× '%s' (cap=%.1f, used=%.1f)",
							count, p.itemId.c_str(), actor->def().inventory_capacity,
							actor->inventory->totalValue());
						nudgeR(ActionRejectCode::ItemNotInInventory, buf);
						break;
					}

					src->inventory->remove(p.itemId, count);
					actor->inventory->add(p.itemId, count);

					printf("[Server] Relocate/take: entity %u took %d×%s from entity %u\n",
						actor->id(), count, p.itemId.c_str(), src->id());

					if (m_callbacks.onInventoryChange) {
						m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
						m_callbacks.onInventoryChange(src->id(), *src->inventory);
					}
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
						if (target->def().isLiving()) {
							int count = 1 + (rand() % 2);
							glm::vec3 lootPos = target->position + glm::vec3(0, 0.3f, 0);
							glm::vec3 fwd = (dist > 0.1f) ? glm::normalize(diff) : glm::vec3(0, 0, 1);
							EntityId lootId = m_world->entities.spawn(ItemName::ItemEntity, lootPos,
								{{Prop::ItemType, std::string("meat")},
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

				// Notify structure system of block destruction
				EntityId sid = m_structureCacher.lookup(bp);
				if (sid != ENTITY_NONE) {
					Entity* se = m_world->entities.get(sid);
					if (se && se->structure) {
						if (bp == se->structure->anchorPos) {
							// Anchor destroyed → remove structure entity
							m_structureCacher.unregisterStructure(sid);
							se->removed = true;
							m_incompleteStructures.erase(sid);
						} else {
							m_incompleteStructures.insert(sid);
						}
					}
				}
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
					m_world->entities.spawn(ItemName::ItemEntity, spawnPos,
						{{Prop::ItemType, p.toItem}, {Prop::Count, p.toCount}, {Prop::Age, 0.0f}});
				} else {
					// Default: add directly to actor's inventory (Self)
					if (actor->inventory) {
						actor->inventory->add(p.toItem, p.toCount);
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

} // namespace civcraft

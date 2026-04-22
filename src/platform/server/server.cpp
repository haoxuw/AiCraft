#include "server/server.h"
#include "logic/material_values.h"
#include "debug/move_stuck_log.h"
#include <cassert>
#include <cmath>

namespace civcraft {

// Rule 0: the server accepts exactly four primitives. High-level intents
// (follow, flee, attack, pathfind, …) are resolved entirely on the agent
// client and must compile down to one of these before hitting the wire.
static_assert(ActionProposal::Move     == 0, "Rule 0: primitive order fixed");
static_assert(ActionProposal::Relocate == 1, "Rule 0: primitive order fixed");
static_assert(ActionProposal::Convert  == 2, "Rule 0: primitive order fixed");
static_assert(ActionProposal::Interact == 3, "Rule 0: primitive order fixed");

void GameServer::resolveActions(float dt) {
	auto proposals = m_world->proposals.drain();
	m_actionStats.resolved += (int)proposals.size();

	// Rule 0 gate: reject anything that isn't one of the 4 primitives.
	for (auto& p : proposals) {
		assert((p.type == ActionProposal::Move     ||
		        p.type == ActionProposal::Relocate ||
		        p.type == ActionProposal::Convert  ||
		        p.type == ActionProposal::Interact)
		       && "Rule 0: server only accepts Move/Relocate/Convert/Interact");
	}

	// Decrement + erase so the cooldown map stays bounded.
	for (auto it = m_clientPosRejectCooldown.begin(); it != m_clientPosRejectCooldown.end();) {
		if (--it->second <= 0) it = m_clientPosRejectCooldown.erase(it);
		else                    ++it;
	}

	// One nudge-back per entity per tick.
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

			// Rule 3: trust clientPos iff within tolerance and not inside
			// solid; else reject → server-authoritative. Cooldown swallows
			// stale in-flight proposals (see feedback_client_pos_reject_policy.md).
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
						// Client already collided — skip server physics this tick.
						e->skipPhysics = true;
					} else {
						// Subsequent stale in-flight proposals suppressed.
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
			// Also ignore hasClientPos for Y branch during cooldown — stale
			// post-physics Y would desync vertically.
			if (inCooldown) p.hasClientPos = false;

			// Anti-cheat: sprint 2.5x, fly uncapped, tolerance 3.5x walk.
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

			// 10-block lookahead for client prediction.
			float hLen = std::sqrt(p.desiredVel.x * p.desiredVel.x + p.desiredVel.z * p.desiredVel.z);
			if (hLen > 0.01f) {
				glm::vec3 dir = {p.desiredVel.x / hLen, 0, p.desiredVel.z / hLen};
				e->moveTarget = e->position + dir * 10.0f;
				e->moveSpeed = hLen;
				// Yaw smoothed per-tick from velocity in GameServer::tick.
			} else {
				e->moveTarget = e->position;
				e->moveSpeed = 0;
			}
			e->lookPitch = p.lookPitch;
			e->lookYaw   = p.lookYaw;

			if (p.hasClientPos) {
				// Client ran gravity/jump → trust its Y.
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

			// Resend inventory to correct client optimistic state (dedup/tick).
			// `why` carries item/src→dst/distance so rejects are diagnosable.
			auto nudgeR = [&](ActionRejectCode code, const std::string& why) {
				logActionReject("Relocate", p.actorId, rejectCodeName(code), why.c_str());
				if (nudgedThisTick.count(p.actorId)) return;
				nudgedThisTick.insert(p.actorId);
				if (actor->inventory && m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			};

			if (p.relocateTo.kind == Container::Kind::Ground) {
				// Drop → spawn item entity.
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
				// Store into chest/creature inventory.
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
					// No reject code for out-of-range today; log-and-drop.
					printf("[Server] Relocate rejected entity=%u (%s)\n", actor->id(), buf);
					break;
				}

				int totalTransferred = 0;
				if (!p.itemId.empty()) {
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
					// Transfer all.
					for (auto& [itemIdStr, count] : actor->inventory->items()) {
						target->inventory->add(itemIdStr, count);
						totalTransferred += count;
					}
					actor->inventory->clear();
				}

				(void)totalTransferred;

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
					// Pickup: validate actor's pickup_range.
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
					src->removalReason = (uint8_t)EntityRemovalReason::Despawned;

					if (m_callbacks.onInventoryChange)
						m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
				} else if (src->inventory) {
					// Take from chest/creature inventory.
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

			// Resend inventory to correct optimistic state; dedup/tick.
			// SourceBlockGone on a block harvest is a benign multi-agent race
			// (villager A chops first, B's LocalWorld hasn't drained the
			// S_BLOCK yet, B re-emits Convert). The agent auto-corrects once
			// the block update arrives, so skip the noisy stderr log but keep
			// the inventory nudge — the optimistic log-pickup still needs to
			// be rolled back. Other codes are genuine bugs and stay loud.
			//
			// TODO(predicted-break snap-back): we don't re-emit onBlockChange
			// on reject, so a player's optimistically-broken block would
			// leave a phantom hole if rejected. Today the only reachable
			// player reject is SourceBlockGone (block already AIR, prediction
			// correct); other codes are config bugs. Revisit if that changes.
			auto nudge = [&](ActionRejectCode code, const std::string& why) {
				char detail[256];
				std::snprintf(detail, sizeof(detail), "from=%s to=%s: %s",
				              p.fromItem.c_str(), p.toItem.c_str(), why.c_str());
				if (code != ActionRejectCode::SourceBlockGone)
					logActionReject("Convert", p.actorId, rejectCodeName(code), detail);
				if (nudgedThisTick.count(p.actorId)) return;
				nudgedThisTick.insert(p.actorId);
				if (actor->inventory && m_callbacks.onInventoryChange)
					m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			};

			// Act on target's HP/inventory (e.g. attack).
			if (p.convertFrom.kind == Container::Kind::Entity) {
				Entity* target = m_world->entities.get(p.convertFrom.entityId);
				if (!target || target->removed) break;

				if (p.fromItem == "hp") {
					if (!target->def().isLiving()) break;

					// TODO(lag-comp, gap #5): rewind `target->position` to where it was
					// `rtt/2 + clientInterpDelay` ago before validating the hit.
					// Needs: (a) per-entity position history ring (~500ms) in Entity or
					// EntityManager, (b) `clientRenderTime` field on Convert proposals
					// (or RTT from heartbeat echo), (c) range/LOS check against the
					// rewound position here. Cap rewind at ~500ms to bound cheat window.
					// See plans/netcode_lag_compensation.md (to be written).
					int dmg = std::max(p.fromCount, 1);
					int hp = target->hp();
					target->setHp(std::max(hp - dmg, 0));

					glm::vec3 diff = target->position - actor->position;
					float dist = glm::length(diff);
					glm::vec3 kb = (dist > 0.1f) ? glm::normalize(diff) : glm::vec3(0, 0, 1);
					target->velocity += kb * 4.0f + glm::vec3(0, 3.0f, 0);

					if (target->hp() <= 0) {
						target->removed = true;
						target->removalReason = (uint8_t)EntityRemovalReason::Died;

						// Animal loot.
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
					// Destroy specific item from target.
					if (target->inventory && target->inventory->has(p.fromItem))
						target->inventory->remove(p.fromItem, p.fromCount);
				}
				break;
			}

			const bool fromBlock = (p.convertFrom.kind == Container::Kind::Block);
			const bool intoBlock = (p.convertInto.kind == Container::Kind::Block);

			// Position-authoritative block source (I3 of docs/22_APPEARANCE.md):
			// when the source is a block, server derives fromItem from the actual
			// block at the position. Client-supplied fromItem is ignored — this
			// eliminates SourceBlockTypeMismatch rejects when a painter mutates
			// the block type mid-plan. Must run before value-conservation so the
			// material-value lookup uses the authoritative id.
			if (fromBlock) {
				auto& bp = p.convertFrom.pos;
				BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
				if (bid == BLOCK_AIR) {
					char buf[96]; std::snprintf(buf, sizeof(buf),
						"source block at (%d,%d,%d) is already air", bp.x, bp.y, bp.z);
					nudge(ActionRejectCode::SourceBlockGone, buf); break;
				}
				p.fromItem = m_world->blocks.get(bid).string_id;
			}

			// Rule 0: output value must not exceed input.
			float inVal  = getMaterialValue(p.fromItem) * (float)p.fromCount;
			float outVal = getMaterialValue(p.toItem)   * (float)p.toCount;
			if (outVal > inVal + 0.001f) {
				char buf[128];
				std::snprintf(buf, sizeof(buf),
					"output value %.2f (×%d) exceeds input %.2f (×%d)",
					outVal, p.toCount, inVal, p.fromCount);
				nudge(ActionRejectCode::ValueConservationViolated, buf);
				break;
			}

			// Validate placement BEFORE consuming source (prevents item loss on race).
			if (intoBlock) {
				auto& pp = p.convertInto.pos;
				if (m_world->getBlock(pp.x, pp.y, pp.z) != BLOCK_AIR) {
					char buf[96]; std::snprintf(buf, sizeof(buf),
						"placement target (%d,%d,%d) is not air", pp.x, pp.y, pp.z);
					nudge(ActionRejectCode::PlacementTargetOccupied, buf); break;
				}
				if (!m_world->blocks.find(p.toItem)) {
					nudge(ActionRejectCode::UnknownBlockType,
						"toItem '" + p.toItem + "' is not a registered block"); break;
				}
				if (!m_world->getChunk(worldToChunk(pp.x, pp.y, pp.z))) {
					char buf[96]; std::snprintf(buf, sizeof(buf),
						"placement target chunk for (%d,%d,%d) not loaded", pp.x, pp.y, pp.z);
					nudge(ActionRejectCode::ChunkNotLoaded, buf); break;
				}
			}

			// Consume source.
			if (fromBlock) {
				auto& bp = p.convertFrom.pos;
				// Early derivation above already proved bid != AIR and set fromItem.
				BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
				m_world->removeBlockState(bp.x, bp.y, bp.z);
				ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
				Chunk* c = m_world->getChunk(cp);
				if (!c) break;
				int lx = ((bp.x % 16) + 16) % 16, ly = ((bp.y % 16) + 16) % 16, lz = ((bp.z % 16) + 16) % 16;
				uint8_t oldP2 = c->getParam2(lx, ly, lz);
				uint8_t oldApp = c->getAppearance(lx, ly, lz);
				c->set(lx, ly, lz, BLOCK_AIR);
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(BlockChange{bp, bid, BLOCK_AIR, oldP2, 0, oldApp, 0},
					BroadcastPriority::High);

				// Structure damage: remove entity if anchor destroyed.
				EntityId sid = m_structureCacher.lookup(bp);
				if (sid != ENTITY_NONE) {
					Entity* se = m_world->entities.get(sid);
					if (se && se->structure) {
						if (bp == se->structure->anchorPos) {
							m_structureCacher.unregisterStructure(sid);
							se->removed = true;
							se->removalReason = (uint8_t)EntityRemovalReason::Despawned;
							m_incompleteStructures.erase(sid);
						} else {
							m_incompleteStructures.insert(sid);
						}
					}
				}
			} else if (p.fromItem == "hp") {
				int hp = actor->hp();
				actor->setHp(std::max(hp - p.fromCount, 0));
			} else {
				// Consume from actor's inventory.
				if (!actor->inventory) break;
				if (!actor->inventory->has(p.fromItem)) {
					char buf[160]; std::snprintf(buf, sizeof(buf),
						"actor has 0× '%s' (need %d)", p.fromItem.c_str(), p.fromCount);
					nudge(ActionRejectCode::ItemNotInInventory, buf); break;
				}
				actor->inventory->remove(p.fromItem, p.fromCount);
			}

			// Produce output.
			if (intoBlock) {
				// Target was pre-validated clear.
				auto& pp = p.convertInto.pos;
				const BlockDef* placedDef = m_world->blocks.find(p.toItem);
				ChunkPos cp = worldToChunk(pp.x, pp.y, pp.z);
				Chunk* c = m_world->getChunk(cp);
				if (!c || !placedDef) break;  // rare race vs pre-check

				// Door: auto-detect hinge from neighbors.
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

				// Don't let actor place inside their own body.
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
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(BlockChange{pp, BLOCK_AIR, placedBid, 0, placeP2, 0, 0},
					BroadcastPriority::High);

				if (placedDef->behavior == BlockBehavior::Active)
					m_world->setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);
			} else if (p.toItem == "hp") {
				int hp = actor->hp();
				if (hp < actor->def().max_hp)
					actor->setHp(std::min(hp + p.toCount, actor->def().max_hp));
			} else if (!p.toItem.empty()) {
				if (p.convertInto.kind == Container::Kind::Ground) {
					glm::vec3 spawnPos = fromBlock
						? (glm::vec3(p.convertFrom.pos) + glm::vec3(0.5f, 0.5f, 0.5f))
						: (actor->position + glm::vec3(0, 0.3f, 0));
					m_world->entities.spawn(ItemName::ItemEntity, spawnPos,
						{{Prop::ItemType, p.toItem}, {Prop::Count, p.toCount}, {Prop::Age, 0.0f}});
				} else {
					// Default: into actor's inventory.
					if (actor->inventory) {
						actor->inventory->add(p.toItem, p.toCount);
					}
				}
			}

			if (actor->inventory && m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
			break;
		}

		case ActionProposal::Interact: {
			auto& bp = p.blockPos;

			BlockId bid = m_world->getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = m_world->blocks.get(bid);

			// Appearance mutation (I3): TYPE_INTERACT with appearanceIdx >= 0
			// writes into the palette; BlockId/param2 are preserved. No-op if
			// source block is AIR.
			if (p.appearanceIdx >= 0) {
				if (bid == BLOCK_AIR) break;
				uint8_t clamped = bdef.clampAppearance((uint8_t)p.appearanceIdx);
				uint8_t oldApp = m_world->setAppearance(bp.x, bp.y, bp.z, clamped);
				if (oldApp != clamped && m_callbacks.onBlockChange) {
					uint8_t p2 = 0;
					ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
					if (Chunk* c = m_world->getChunk(cp)) {
						int lx = ((bp.x%16)+16)%16, ly = ((bp.y%16)+16)%16, lz = ((bp.z%16)+16)%16;
						p2 = c->getParam2(lx, ly, lz);
					}
					m_callbacks.onBlockChange(BlockChange{bp, bid, bid, p2, p2, oldApp, clamped},
						BroadcastPriority::High);
				}
				break;
			}

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

			// Door: toggle + propagate vertically through connected door stack.
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
				uint8_t oldApp = c->getAppearance(lx, ly, lz);
				BlockId oldId = c->get(lx, ly, lz);  // MUST read before set
				c->set(lx, ly, lz, id, p2);
				glm::ivec3 pos{x, y, z};
				if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(BlockChange{pos, oldId, id, p2, p2, oldApp, 0},
					BroadcastPriority::High);
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

		default: break;

		} // switch
	} // for
} // resolveActions

} // namespace civcraft

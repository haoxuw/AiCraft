#include "server/server.h"
#include <cmath>

namespace agentworld {

void GameServer::resolveActions(float dt) {
	auto proposals = m_world->actions.drain();

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

			if (m_callbacks.onBlockBreak) m_callbacks.onBlockBreak(glm::vec3(bp), bdef.color_top, 1);
			if (m_callbacks.onBreakText) m_callbacks.onBreakText(glm::vec3(bp), bdef.display_name);
			m_world->removeBlockState(bp.x, bp.y, bp.z);

			if (actor && actor->inventory) {
				std::string dropType = bdef.drop.empty() ? bdef.string_id : bdef.drop;
				if (!dropType.empty() && dropType != BlockType::Air) {
					if (m_creative) {
						actor->inventory->add(dropType, 1);
					} else {
						glm::vec3 dropPos = glm::vec3(bp) + glm::vec3(0.5f, 0.5f, 0.5f);
						m_world->entities.spawn(EntityType::ItemEntity, dropPos,
							{{Prop::ItemType, dropType}, {Prop::Count, 1}, {Prop::Age, 0.0f}});
					}
				}
			}

			c->set(((bp.x % 16) + 16) % 16, ((bp.y % 16) + 16) % 16,
			       ((bp.z % 16) + 16) % 16, BLOCK_AIR);
			if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(bp, BLOCK_AIR);
			if (m_callbacks.onChunkDirty) {
				m_callbacks.onChunkDirty(cp);
				for (int a = 0; a < 3; a++) {
					int coords[] = {bp.x, bp.y, bp.z};
					int l = ((coords[a] % 16) + 16) % 16;
					if (l == 0 || l == 15) {
						glm::ivec3 o(0); o[a] = (l == 0) ? -1 : 1;
						m_callbacks.onChunkDirty({cp.x + o.x, cp.y + o.y, cp.z + o.z});
					}
				}
			}
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

			BlockId placedBid = m_world->blocks.getId(p.blockType);
			c->set(((pp.x % 16) + 16) % 16, ((pp.y % 16) + 16) % 16,
			       ((pp.z % 16) + 16) % 16, placedBid);
			if (m_callbacks.onBlockChange) m_callbacks.onBlockChange(pp, placedBid);

			if (m_callbacks.onChunkDirty) {
				m_callbacks.onChunkDirty(cp);
				for (int a = 0; a < 3; a++) {
					int coords[] = {pp.x, pp.y, pp.z};
					int l = ((coords[a] % 16) + 16) % 16;
					if (l == 0 || l == 15) {
						glm::ivec3 o(0); o[a] = (l == 0) ? -1 : 1;
						m_callbacks.onChunkDirty({cp.x + o.x, cp.y + o.y, cp.z + o.z});
					}
				}
			}

			if (placedDef->behavior == BlockBehavior::Active)
				m_world->setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);

			if (m_callbacks.onBlockPlace)
				m_callbacks.onBlockPlace(glm::vec3(pp), placedDef->sound_place);

			if (actor && actor->inventory && !m_creative)
				actor->inventory->remove(p.blockType, 1);
			// Sync inventory after block place
			if (actor && actor->inventory && m_callbacks.onInventoryChange)
				m_callbacks.onInventoryChange(actor->id(), *actor->inventory);
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
			if (p.blockType.empty()) break;  // no item type specified

			// Server validates: actor must be alive, count must be reasonable
			if (!actor->def().isLiving()) break;
			int count = std::clamp(p.itemCount, 1, 64);

			// Spawn item entity at actor's feet
			glm::vec3 dropPos = actor->position + glm::vec3(0, 0.3f, 0);
			m_world->entities.spawn(EntityType::ItemEntity, dropPos,
				{{Prop::ItemType, p.blockType},
				 {Prop::Count, count},
				 {Prop::Age, 0.0f}});
			break;
		}

		case ActionProposal::GrowCrop:
		case ActionProposal::Attack:
		case ActionProposal::PickupItem:
			break;
		}
	}
}

} // namespace agentworld

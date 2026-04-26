#pragma once

// 1:1 block-pos → structure EntityId map. O(1) on block change.
// Overlap prevented at registration: canRegister() rejects if any slot is claimed.

#include "logic/constants.h"
#include "server/structure_blueprint.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace solarium {

class StructureBlockCacher {
public:
	// False if any blueprint slot (or anchor) is already claimed.
	bool canRegister(const StructureBlueprint& bp, glm::ivec3 anchor) const {
		glm::ivec3 anchorWorld = anchor + bp.anchor.offset;
		if (m_blockToStructure.count(pack(anchorWorld))) return false;
		for (const auto& slot : bp.blocks) {
			if (m_blockToStructure.count(pack(anchor + slot.offset))) return false;
		}
		return true;
	}

	// Caller must canRegister() first.
	void registerStructure(EntityId id, const StructureBlueprint& bp, glm::ivec3 anchor) {
		std::vector<PackedPos> positions;
		glm::ivec3 anchorWorld = anchor + bp.anchor.offset;
		PackedPos ap = pack(anchorWorld);
		m_blockToStructure[ap] = id;
		m_anchorPositions.insert(ap);
		positions.push_back(ap);
		for (const auto& slot : bp.blocks) {
			PackedPos pp = pack(anchor + slot.offset);
			m_blockToStructure[pp] = id;
			positions.push_back(pp);
		}
		m_structureToBlocks[id] = std::move(positions);
	}

	void unregisterStructure(EntityId id) {
		auto it = m_structureToBlocks.find(id);
		if (it == m_structureToBlocks.end()) return;
		for (PackedPos pp : it->second) {
			m_blockToStructure.erase(pp);
			m_anchorPositions.erase(pp);
		}
		m_structureToBlocks.erase(it);
	}

	// ENTITY_NONE if unclaimed.
	EntityId lookup(glm::ivec3 pos) const {
		auto it = m_blockToStructure.find(pack(pos));
		return it != m_blockToStructure.end() ? it->second : ENTITY_NONE;
	}

	bool isAnchor(glm::ivec3 pos) const {
		return m_anchorPositions.count(pack(pos)) > 0;
	}

	size_t totalBlocks() const { return m_blockToStructure.size(); }
	size_t totalStructures() const { return m_structureToBlocks.size(); }

private:
	using PackedPos = uint64_t;

	// 21 bits per axis → coords in [-2M, +2M].
	static PackedPos pack(glm::ivec3 p) {
		return ((uint64_t)(uint32_t)p.x)
		     | ((uint64_t)(uint32_t)p.y << 21)
		     | ((uint64_t)(uint32_t)p.z << 42);
	}

	std::unordered_map<PackedPos, EntityId>              m_blockToStructure;
	std::unordered_map<EntityId, std::vector<PackedPos>> m_structureToBlocks;
	std::unordered_set<PackedPos>                        m_anchorPositions;
};

} // namespace solarium

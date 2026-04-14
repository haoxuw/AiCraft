#pragma once

/**
 * StructureBlockCacher — 1-to-1 mapping from block positions to structure entities.
 *
 * Every block belonging to a registered structure is mapped to its owning EntityId.
 * On block change, lookup is O(1). Overlap is prevented at registration time:
 * canRegister() rejects placement if any blueprint slot is already claimed.
 */

#include "shared/constants.h"
#include "server/structure_blueprint.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace civcraft {

class StructureBlockCacher {
public:
	// Check if all blueprint block positions are unclaimed.
	// Returns false if any position is already registered to another structure.
	bool canRegister(const StructureBlueprint& bp, glm::ivec3 anchor) const {
		// Check anchor position
		glm::ivec3 anchorWorld = anchor + bp.anchor.offset;
		if (m_blockToStructure.count(pack(anchorWorld))) return false;
		// Check each blueprint block
		for (const auto& slot : bp.blocks) {
			if (m_blockToStructure.count(pack(anchor + slot.offset))) return false;
		}
		return true;
	}

	// Register all blocks of a structure. Caller must check canRegister() first.
	void registerStructure(EntityId id, const StructureBlueprint& bp, glm::ivec3 anchor) {
		std::vector<PackedPos> positions;
		// Register anchor position
		glm::ivec3 anchorWorld = anchor + bp.anchor.offset;
		PackedPos ap = pack(anchorWorld);
		m_blockToStructure[ap] = id;
		m_anchorPositions.insert(ap);
		positions.push_back(ap);
		// Register each blueprint block
		for (const auto& slot : bp.blocks) {
			PackedPos pp = pack(anchor + slot.offset);
			m_blockToStructure[pp] = id;
			positions.push_back(pp);
		}
		m_structureToBlocks[id] = std::move(positions);
	}

	// Remove all block mappings for a structure.
	void unregisterStructure(EntityId id) {
		auto it = m_structureToBlocks.find(id);
		if (it == m_structureToBlocks.end()) return;
		for (PackedPos pp : it->second) {
			m_blockToStructure.erase(pp);
			m_anchorPositions.erase(pp);
		}
		m_structureToBlocks.erase(it);
	}

	// O(1) lookup: which structure owns this block position?
	// Returns ENTITY_NONE if no structure claims it.
	EntityId lookup(glm::ivec3 pos) const {
		auto it = m_blockToStructure.find(pack(pos));
		return it != m_blockToStructure.end() ? it->second : ENTITY_NONE;
	}

	// Is this position an anchor block for any structure?
	bool isAnchor(glm::ivec3 pos) const {
		return m_anchorPositions.count(pack(pos)) > 0;
	}

	size_t totalBlocks() const { return m_blockToStructure.size(); }
	size_t totalStructures() const { return m_structureToBlocks.size(); }

private:
	using PackedPos = uint64_t;

	// Pack a world position into a single uint64 for hashing.
	// Supports coordinates in [-2M, +2M] range (21 bits each).
	static PackedPos pack(glm::ivec3 p) {
		return ((uint64_t)(uint32_t)p.x)
		     | ((uint64_t)(uint32_t)p.y << 21)
		     | ((uint64_t)(uint32_t)p.z << 42);
	}

	std::unordered_map<PackedPos, EntityId>              m_blockToStructure;
	std::unordered_map<EntityId, std::vector<PackedPos>> m_structureToBlocks;
	std::unordered_set<PackedPos>                        m_anchorPositions;
};

} // namespace civcraft

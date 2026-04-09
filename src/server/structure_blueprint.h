#pragma once

/**
 * Structure blueprints — define what blocks compose a structure entity.
 *
 * Blueprints are loaded from Python artifacts (artifacts/structures/)
 * via the same py::exec() pattern as world configs (python_bridge.cpp).
 *
 * The StructureBlueprintManager owns all loaded blueprints and provides
 * firstMissingBlock() for integrity checks.
 */

#include "shared/constants.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <filesystem>

namespace modcraft {

class Entity; // forward — defined in shared/entity.h

// One block that forms part of a structure's footprint.
// Offsets are relative to the structure's anchor position.
struct BlockSlot {
	glm::ivec3  offset;
	std::string block_type;  // expected block type string ID (e.g. "base:trunk")
	                         // empty = any non-air block satisfies this slot
};

// The anchor block that determines structure lifetime.
// If the anchor is destroyed, the structure entity is removed.
struct AnchorDef {
	glm::ivec3  offset      = {0, -1, 0}; // relative to entity position (underground by default)
	std::string block_type  = "base:root";
	int         hardness    = 3;           // stored for future use (no enforcement path yet)
};

// Full definition of a structure type, loaded from a Python blueprint file.
struct StructureBlueprint {
	std::string id;               // "base:tree"
	std::string display_name;
	AnchorDef   anchor;
	std::vector<BlockSlot> blocks;
	bool  regenerates      = false;
	float regen_interval_s = 60.0f; // seconds between placing ONE missing block back
};

// Forward-declared — implemented in python_bridge.cpp alongside loadWorldConfig().
bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out);

class StructureBlueprintManager {
public:
	// Load all blueprint .py files from artifacts/structures/base/ and player/.
	void loadAll(const std::string& baseDir) {
		for (const char* sub : {"base", "player"}) {
			std::string dir = baseDir + "/" + sub;
			if (!std::filesystem::is_directory(dir)) continue;
			for (auto& entry : std::filesystem::directory_iterator(dir)) {
				if (entry.path().extension() != ".py") continue;
				if (entry.path().filename() == "__init__.py") continue;
				StructureBlueprint bp;
				if (loadStructureBlueprint(entry.path().string(), bp)) {
					m_blueprints[bp.id] = std::move(bp);
				}
			}
		}
	}

	const StructureBlueprint* get(const std::string& id) const {
		auto it = m_blueprints.find(id);
		return it != m_blueprints.end() ? &it->second : nullptr;
	}

	const std::unordered_map<std::string, StructureBlueprint>& all() const {
		return m_blueprints;
	}

	// Returns the first BlockSlot whose world block doesn't match the blueprint,
	// or nullopt if every slot is satisfied (structure is complete).
	// blockAt(x, y, z) must return the block type string ID at that world position.
	std::optional<BlockSlot> firstMissingBlock(
		const Entity& e,
		const std::function<std::string(int,int,int)>& blockAt) const;

private:
	std::unordered_map<std::string, StructureBlueprint> m_blueprints;
};

} // namespace modcraft

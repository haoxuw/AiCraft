#pragma once

// What blocks compose a structure entity. Loaded from artifacts/structures/
// via the py::exec() pattern in python_bridge.cpp. Manager owns all blueprints
// and provides firstMissingBlock() for integrity checks.

#include "logic/constants.h"
#include "logic/entity.h"   // StructureFeature (shared with live entities)
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <filesystem>

namespace civcraft {

class Entity; // shared/entity.h

// Offsets relative to anchor. Empty block_type = any non-air satisfies.
struct BlockSlot {
	glm::ivec3  offset;
	std::string block_type;  // "logs", etc.
};

// Destroyed anchor → structure entity removed.
struct AnchorDef {
	glm::ivec3  offset      = {0, -1, 0};  // relative to entity; underground default
	std::string block_type  = "root";
	int         hardness    = 3;           // future use
};

struct StructureBlueprint {
	std::string id;               // "tree"
	std::string display_name;
	AnchorDef   anchor;
	std::vector<BlockSlot> blocks;
	bool  regenerates      = false;
	float regen_interval_s = 60.0f;  // seconds between one block replacement
	// Decorators applied when a structure entity spawns (seasonal leaves,
	// flame FX, etc). Server tick dispatcher walks them per-entity.
	std::vector<StructureFeature> features;
};

// Implemented in python_bridge.cpp alongside loadWorldConfig().
bool loadStructureBlueprint(const std::string& filePath, StructureBlueprint& out);

class StructureBlueprintManager {
public:
	// Scans artifacts/structures/base.
	void loadAll(const std::string& baseDir) {
		std::string dir = baseDir + "/base";
		if (!std::filesystem::is_directory(dir)) return;
		for (auto& entry : std::filesystem::directory_iterator(dir)) {
			if (entry.path().extension() != ".py") continue;
			if (entry.path().filename() == "__init__.py") continue;
			StructureBlueprint bp;
			if (loadStructureBlueprint(entry.path().string(), bp)) {
				m_blueprints[bp.id] = std::move(bp);
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

	// First mismatching slot, or nullopt if structure is complete.
	// blockAt returns the block type string ID at a world position.
	std::optional<BlockSlot> firstMissingBlock(
		const Entity& e,
		const std::function<std::string(int,int,int)>& blockAt) const;

private:
	std::unordered_map<std::string, StructureBlueprint> m_blueprints;
};

} // namespace civcraft

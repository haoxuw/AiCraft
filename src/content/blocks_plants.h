#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerPlantBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace SN = Sound;

	reg.registerBlock({BT::Log, "Trunk",
		{0.38f,0.28f,0.14f},{0.52f,0.38f,0.20f},{0.38f,0.28f,0.14f},
		true,false, "",64,0, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Wood, "Wood",
		{0.50f,0.38f,0.18f},{0.42f,0.28f,0.12f},{0.50f,0.38f,0.18f},
		true,false, "",64,0, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Planks, "Planks",
		{0.68f,0.52f,0.30f},{0.64f,0.47f,0.24f},{0.68f,0.52f,0.30f},
		true,false, "",64,0, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Leaves, "Leaves",
		{0.18f,0.48f,0.10f},{0.20f,0.45f,0.12f},{0.20f,0.45f,0.12f},
		true,false, "",64,0, "",SN::DigLeaves,""});
}

} // namespace modcraft::builtin

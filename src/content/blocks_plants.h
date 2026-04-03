#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace agentworld::builtin {

// Plant blocks: wood logs, leaves.
inline void registerPlantBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace CT = Category;
	namespace GR = Group;
	namespace TL = Tool;
	namespace SN = Sound;

	reg.registerBlock({BT::Wood, "Wood Log", CT::Plant,
		{0.50f,0.38f,0.18f},{0.42f,0.28f,0.12f},{0.50f,0.38f,0.18f},
		true,false, 2.0f,TL::Axe,"",64,0,
		{{GR::Choppy,2},{GR::Tree,1},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Planks, "Planks", CT::Plant,
		{0.68f,0.52f,0.30f},{0.64f,0.47f,0.24f},{0.68f,0.52f,0.30f},
		true,false, 2.0f,TL::Axe,"",64,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Leaves, "Leaves", CT::Plant,
		{0.18f,0.48f,0.10f},{0.20f,0.45f,0.12f},{0.20f,0.45f,0.12f},
		true,false, 0.3f,"","",64,0,
		{{GR::Snappy,3},{GR::Flammable,2}}, "",SN::DigLeaves,""});
}

} // namespace agentworld::builtin

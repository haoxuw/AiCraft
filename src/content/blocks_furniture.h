#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace agentworld::builtin {

// Furniture blocks: bed, crafting table, etc.
inline void registerFurnitureBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace CT = Category;
	namespace GR = Group;
	namespace TL = Tool;
	namespace SN = Sound;

	// Bed: red blanket top, gray pillow side, wood-frame bottom
	reg.registerBlock({BT::Bed, "Bed", CT::Crafted,
		{0.62f,0.14f,0.14f},{0.55f,0.54f,0.57f},{0.60f,0.44f,0.24f},
		true,false, 0.2f,"","",1,0,
		{}, "",SN::DigWood,SN::StepWood});
}

} // namespace agentworld::builtin

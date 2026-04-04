#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace agentica::builtin {

// Furniture blocks: bed, stairs, etc.
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

	// Stair: half-height block (collision_height=0.5). Physics step-up carries
	// entities smoothly up each step without requiring a full-block jump.
	// Color: warm plank-wood to visually distinguish from wall/floor stone.
	reg.registerBlock({BT::Stair, "Stair", CT::Crafted,
		{0.68f,0.52f,0.30f},{0.60f,0.44f,0.24f},{0.60f,0.44f,0.24f},
		true,false, 1.5f,TL::Axe,"",64,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		0.5f});  // collision_height: occupies bottom half of cell only
}

} // namespace agentica::builtin

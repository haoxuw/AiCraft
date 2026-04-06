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

	// Stair: half-height physics + stair-shaped visual (2-box mesh).
	// param2 (FourDir) controls which direction the step rises toward.
	reg.registerBlock({BT::Stair, "Stair", CT::Crafted,
		{0.68f,0.52f,0.30f},{0.60f,0.44f,0.24f},{0.60f,0.44f,0.24f},
		true,false, 1.5f,TL::Axe,"",64,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		0.5f, MeshType::Stair, Param2Type::FourDir});

	// Door (closed): solid thin panel on -Z face. Toggles open on interaction.
	reg.registerBlock({BT::Door, "Door", CT::Crafted,
		{0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f},
		true,false, 1.5f,TL::Axe,"",1,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		1.0f, MeshType::Door});

	// Door (open): non-solid thin panel on -X face. Drops door item when broken.
	reg.registerBlock({BT::DoorOpen, "Door (Open)", CT::Crafted,
		{0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f},
		false,false, 1.5f,"",BT::Door,1,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		0.0f, MeshType::DoorOpen});
}

} // namespace agentica::builtin

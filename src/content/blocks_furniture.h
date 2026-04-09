#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerFurnitureBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace SN = Sound;

	reg.registerBlock({BT::Bed, "Bed",
		{0.62f,0.14f,0.14f},{0.55f,0.54f,0.57f},{0.60f,0.44f,0.24f},
		true,false, "",1,0, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Stair, "Stair",
		{0.68f,0.52f,0.30f},{0.60f,0.44f,0.24f},{0.60f,0.44f,0.24f},
		true,false, "",64,0, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		0.5f, MeshType::Stair, Param2Type::FourDir});

	reg.registerBlock({BT::Door, "Door",
		{0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f},
		true,false, "",1,0, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		1.0f, MeshType::Door});

	reg.registerBlock({BT::DoorOpen, "Door (Open)",
		{0.60f,0.44f,0.24f},{0.55f,0.40f,0.20f},{0.55f,0.40f,0.20f},
		false,false, BT::Door,1,0, "",SN::DigWood,SN::StepWood,
		BlockBehavior::Passive, {}, "",
		0.0f, MeshType::DoorOpen});
}

} // namespace modcraft::builtin

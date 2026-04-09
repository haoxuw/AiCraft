#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerActiveBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace PR = Prop;

	reg.registerBlock({BT::TNT, "TNT",
		{0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f},
		true,false, "",64,0, "","","",
		BlockBehavior::Active,
		{{PR::FuseTicks, 0}, {PR::Lit, 0}},
		"base:tnt_block"});

	reg.registerBlock({BT::Wheat, "Wheat",
		{0.70f,0.65f,0.20f},{0.55f,0.50f,0.15f},{0.60f,0.55f,0.18f},
		true,false, BT::WheatSeeds,64,0, "","","",
		BlockBehavior::Active,
		{{PR::GrowthStage, 0}, {PR::MaxStage, 7}},
		"base:wheat_crop"});

	reg.registerBlock({BT::Wire, "Wire",
		{0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f},
		false,false, BT::Wire,64,0, "","","",
		BlockBehavior::Active,
		{{PR::Power, 0}, {PR::MaxPower, 15}},
		"base:wire_block"});

	reg.registerBlock({BT::NANDGate, "NAND Gate",
		{0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f},
		true,false, BT::NANDGate,64,0, "","","",
		BlockBehavior::Active,
		{{PR::InputA, 0}, {PR::InputB, 0}, {PR::Output, 1}},
		"base:nand_gate_block"});
}

} // namespace modcraft::builtin

#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace aicraft::builtin {

// Active blocks: TNT, crops, signals/logic gates.
// These have per-instance state and Python behavior classes.
inline void registerActiveBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace CT = Category;
	namespace GR = Group;
	namespace TL = Tool;
	namespace PR = Prop;
	namespace PY = PyClass;

	reg.registerBlock({BT::TNT, "TNT", CT::Active,
		{0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f},{0.80f,0.25f,0.20f},
		true,false, 0.0f,"","",64,0,
		{{GR::Tnt,1},{GR::Flammable,5}}, "","","",
		BlockBehavior::Active,
		{{PR::FuseTicks, 0}, {PR::Lit, 0}},
		PY::TNTBlock});

	reg.registerBlock({BT::Wheat, "Wheat", CT::Active,
		{0.70f,0.65f,0.20f},{0.55f,0.50f,0.15f},{0.60f,0.55f,0.18f},
		true,false, 0.0f,"",BT::WheatSeeds,64,0,
		{{GR::Cracky,1}}, "","","",
		BlockBehavior::Active,
		{{PR::GrowthStage, 0}, {PR::MaxStage, 7}},
		PY::WheatCrop});

	reg.registerBlock({BT::Wire, "Wire", CT::Active,
		{0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f},{0.60f,0.10f,0.10f},
		false,false, 0.0f,"",BT::Wire,64,0,
		{{GR::SignalGrp,1}}, "","","",
		BlockBehavior::Active,
		{{PR::Power, 0}, {PR::MaxPower, 15}},
		PY::WireBlock});

	reg.registerBlock({BT::NANDGate, "NAND Gate", CT::Active,
		{0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f},{0.30f,0.30f,0.35f},
		true,false, 1.0f,TL::Pickaxe,BT::NANDGate,64,0,
		{{GR::SignalGrp,1},{GR::Logic,1}}, "","","",
		BlockBehavior::Active,
		{{PR::InputA, 0}, {PR::InputB, 0}, {PR::Output, 1}},
		PY::NANDGateBlock});
}

} // namespace aicraft::builtin

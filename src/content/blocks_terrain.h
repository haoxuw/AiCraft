#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace modcraft::builtin {

inline void registerTerrainBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace SN = Sound;

	reg.registerBlock({BT::Air, "Air",
		{0,0,0},{0,0,0},{0,0,0}, false, true,
		"",64,0, "","",""});

	reg.registerBlock({BT::Stone, "Stone",
		{0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f},
		true,false, BT::Cobblestone,64,0, "",SN::DigStone,SN::StepStone});

	reg.registerBlock({BT::Cobblestone, "Cobblestone",
		{0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f},
		true,false, "",64,0, "",SN::DigStone,SN::StepStone});

	reg.registerBlock({BT::Dirt, "Dirt",
		{0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f},
		true,false, "",64,0, "",SN::DigDirt,SN::StepDirt});

	reg.registerBlock({BT::Grass, "Grass Block",
		{0.30f,0.58f,0.18f},{0.40f,0.38f,0.20f},{0.52f,0.34f,0.20f},
		true,false, BT::Dirt,64,0, "",SN::DigDirt,SN::StepGrass});

	reg.registerBlock({BT::Sand, "Sand",
		{0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f},
		true,false, "",64,0, "",SN::DigSand,SN::StepSand});

	reg.registerBlock({BT::Water, "Water",
		{0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f},
		false,true, "",64,0, "","",""});

	reg.registerBlock({BT::Snow, "Snow",
		{0.93f,0.95f,0.97f},{0.90f,0.92f,0.94f},{0.90f,0.92f,0.94f},
		true,false, "",64,0, "",SN::DigSnow,SN::StepSnow});

	reg.registerBlock({BT::Fence, "Fence",
		{0.55f,0.40f,0.22f},{0.50f,0.35f,0.18f},{0.55f,0.40f,0.22f},
		true,false, "",64,0, "",SN::DigWood,SN::StepWood});

	reg.registerBlock({BT::Farmland, "Farmland",
		{0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f},
		true,false, BT::Dirt,64,0, "",SN::DigDirt,SN::StepDirt});

	reg.registerBlock({BT::Glass, "Glass",
		{0.62f,0.83f,0.88f},{0.58f,0.78f,0.84f},{0.62f,0.83f,0.88f},
		true,true, "",64,0, "","",""});

	reg.registerBlock({BT::Portal, "Portal",
		{0.65f,0.0f,1.0f},{0.55f,0.0f,0.90f},{0.65f,0.0f,1.0f},
		false,true, "",64,0, "","",""});

	reg.registerBlock({
		.string_id    = BT::ArcaneStone,
		.display_name = "Arcane Stone",
		.color_top    = {0.28f, 0.06f, 0.48f},
		.color_side   = {0.22f, 0.08f, 0.40f},
		.color_bottom = {0.16f, 0.05f, 0.32f},
		.solid        = true,
		.stack_max    = 64,
		.surface_glow = true,
	});

	reg.registerBlock({
		.string_id    = BT::SpawnPoint,
		.display_name = "Spawn Point",
		.color_top    = {0.95f, 0.78f, 0.05f},
		.color_side   = {0.85f, 0.65f, 0.02f},
		.color_bottom = {0.70f, 0.50f, 0.01f},
		.solid        = true,
		.stack_max    = 1,
		.surface_glow = true,
	});

	reg.registerBlock({BT::Chest, "Chest",
		{0.55f,0.40f,0.20f},{0.50f,0.35f,0.18f},{0.45f,0.30f,0.15f},
		true,false, "",1,0, "",SN::DigWood,SN::StepWood});
}

} // namespace modcraft::builtin

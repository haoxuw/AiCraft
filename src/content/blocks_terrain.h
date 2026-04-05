#pragma once

#include "shared/block_registry.h"
#include "shared/constants.h"

namespace agentica::builtin {

// Natural terrain blocks: stone, dirt, grass, sand, snow, ice, water.
inline void registerTerrainBlocks(BlockRegistry& reg) {
	namespace BT = BlockType;
	namespace CT = Category;
	namespace GR = Group;
	namespace TL = Tool;
	namespace SN = Sound;

	reg.registerBlock({BT::Air, "Air", CT::System,
		{0,0,0},{0,0,0},{0,0,0}, false, true,
		0,"","",64,0, {}, "","",""});

	reg.registerBlock({BT::Stone, "Stone", CT::Terrain,
		{0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f},{0.48f,0.48f,0.50f},
		true,false, 4.0f,TL::Pickaxe,BT::Cobblestone,64,0,
		{{GR::Cracky,3},{GR::Stone,1}}, "",SN::DigStone,SN::StepStone});

	reg.registerBlock({BT::Cobblestone, "Cobblestone", CT::Terrain,
		{0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f},{0.42f,0.42f,0.44f},
		true,false, 3.5f,TL::Pickaxe,"",64,0,
		{{GR::Cracky,2},{GR::Stone,1}}, "",SN::DigStone,SN::StepStone});

	reg.registerBlock({BT::Dirt, "Dirt", CT::Terrain,
		{0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f},{0.52f,0.34f,0.20f},
		true,false, 0.8f,TL::Shovel,"",64,0,
		{{GR::Crumbly,3},{GR::Soil,1}}, "",SN::DigDirt,SN::StepDirt});

	reg.registerBlock({BT::Grass, "Grass Block", CT::Terrain,
		{0.30f,0.58f,0.18f},{0.40f,0.38f,0.20f},{0.52f,0.34f,0.20f},
		true,false, 0.9f,TL::Shovel,BT::Dirt,64,0,
		{{GR::Crumbly,3},{GR::Soil,1},{GR::Spreadable,1}}, "",SN::DigDirt,SN::StepGrass});

	reg.registerBlock({BT::Sand, "Sand", CT::Terrain,
		{0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f},{0.82f,0.77f,0.50f},
		true,false, 0.6f,TL::Shovel,"",64,0,
		{{GR::Crumbly,3},{GR::Sand,1},{GR::Falling,1}}, "",SN::DigSand,SN::StepSand});

	reg.registerBlock({BT::Water, "Water", CT::Terrain,
		{0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f},{0.12f,0.35f,0.70f},
		false,true, 0,"","",64,0,
		{{GR::Liquid,1},{GR::WaterGrp,1}}, "","",""});

	reg.registerBlock({BT::Snow, "Snow", CT::Terrain,
		{0.93f,0.95f,0.97f},{0.90f,0.92f,0.94f},{0.90f,0.92f,0.94f},
		true,false, 0.3f,TL::Shovel,"",64,0,
		{{GR::Crumbly,3},{GR::Snowy,1}}, "",SN::DigSnow,SN::StepSnow});

	// Fence (for animal pens and gardens)
	reg.registerBlock({BT::Fence, "Fence", CT::Crafted,
		{0.55f,0.40f,0.22f},{0.50f,0.35f,0.18f},{0.55f,0.40f,0.22f},
		true,false, 2.0f,TL::Axe,"",64,0,
		{{GR::Choppy,2},{GR::Flammable,2}}, "",SN::DigWood,SN::StepWood});

	// Farmland (tilled soil for crops)
	reg.registerBlock({BT::Farmland, "Farmland", CT::Terrain,
		{0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f},{0.40f,0.28f,0.14f},
		true,false, 0.6f,TL::Shovel,BT::Dirt,64,0,
		{{GR::Crumbly,3},{GR::Soil,1}}, "",SN::DigDirt,SN::StepDirt});

	// Glass (window block — solid so players can't walk through, light blue)
	reg.registerBlock({BT::Glass, "Glass", CT::Crafted,
		{0.62f,0.83f,0.88f},{0.58f,0.78f,0.84f},{0.62f,0.83f,0.88f},
		true,true, 0.3f,"","",64,0,
		{}, "","",""});

	// Chest (storage block)
	reg.registerBlock({BT::Chest, "Chest", CT::Crafted,
		{0.55f,0.40f,0.20f},{0.50f,0.35f,0.18f},{0.45f,0.30f,0.15f},
		true,false, 2.0f,"","",1,0,
		{}, "",SN::DigWood,SN::StepWood});
}

} // namespace agentica::builtin

// LifeCraft — ActionProposal. Mirrors CivCraft Rule 0: exactly four
// server-validated types. The 2D nature of the game lives in the
// payload, never in new opcodes. See docs/00_OVERVIEW.md § Action types.

#pragma once

#include <cstdint>

namespace civcraft::lifecraft::sim {

enum ActionType : uint8_t {
	TYPE_MOVE     = 0,
	TYPE_RELOCATE = 1,
	TYPE_CONVERT  = 2,
	TYPE_INTERACT = 3,
};

// CONVERT sub-kinds — packed into a single opcode per Rule 0.
enum ConvertKind : uint8_t {
	CONVERT_NONE  = 0,
	CONVERT_SPLIT = 1, // spend biomass, spawn child monster for same owner
	CONVERT_GROW  = 2, // spend biomass, scale self up
};

// RELOCATE sub-kinds.
enum RelocateKind : uint8_t {
	RELOCATE_NONE       = 0,
	RELOCATE_PICKUP_FOOD = 1, // food_id → biomass
};

struct ActionProposal {
	ActionType type = TYPE_MOVE;

	// TYPE_MOVE payload
	float target_heading = 0.0f; // radians
	float thrust         = 0.0f; // [0..1]

	// TYPE_RELOCATE payload
	RelocateKind relocate_kind = RELOCATE_NONE;
	uint32_t     food_id       = 0;

	// TYPE_CONVERT payload
	ConvertKind convert_kind = CONVERT_NONE;
	float       convert_amount = 0.0f; // biomass for SPLIT; scale factor for GROW

	// TYPE_INTERACT — placeholder. Biting is automatic from contact (see sim.cpp).
	uint32_t interact_target = 0;

	static ActionProposal move(float heading, float thrust) {
		ActionProposal a;
		a.type = TYPE_MOVE;
		a.target_heading = heading;
		a.thrust = thrust;
		return a;
	}
	static ActionProposal pickup(uint32_t food_id) {
		ActionProposal a;
		a.type = TYPE_RELOCATE;
		a.relocate_kind = RELOCATE_PICKUP_FOOD;
		a.food_id = food_id;
		return a;
	}
	static ActionProposal split(float biomass) {
		ActionProposal a;
		a.type = TYPE_CONVERT;
		a.convert_kind = CONVERT_SPLIT;
		a.convert_amount = biomass;
		return a;
	}
	static ActionProposal grow(float scale) {
		ActionProposal a;
		a.type = TYPE_CONVERT;
		a.convert_kind = CONVERT_GROW;
		a.convert_amount = scale;
		return a;
	}
};

} // namespace civcraft::lifecraft::sim

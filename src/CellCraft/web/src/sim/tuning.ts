// Mirror of src/CellCraft/sim/tuning.h. Keep names identical so future
// diffs against the C++ source are obvious.

// --- Shape → stats ---------------------------------------------------
export const TURN_K = 120.0;
export const TURN_MIN = 0.4;
export const TURN_MAX = 6.0;

export const MOVE_K = 14000.0;
export const MOVE_MIN = 60.0;
export const MOVE_MAX = 1100.0;

export const DENSITY = 0.02;

// --- World -----------------------------------------------------------
export const DEFAULT_MAP_RADIUS = 1500.0;
export const BOUNDARY_K = 600.0;

// --- Combat / biomass ------------------------------------------------
export const CONTACT_K = 0.18;
export const DEFENDER_THICKNESS_K = 5.0;
export const SEPARATION_IMPULSE = 60.0;

// Part-gated damage (matches sim/tuning.h:58-67)
export const PART_DMG_BASE_SPIKE = 4.0;
export const PART_DMG_BASE_TEETH = 3.0;
export const PART_DMG_BASE_HORN = 6.0;
export const PART_DMG_BASE_VENOM_SPIKE = 5.0;
export const PART_CONTACT_RADIUS_K = 12.0;
export const PART_DMG_K_SPEED = 0.02;
export const PART_DMG_K_MIN = 0.35;

// Hard overlap resolution
export const OVERLAP_PUSH_PAD = 1.0;
export const OVERLAP_MAX_PUSH = 40.0;

export const DEATH_BIOMASS_FRAC = 0.6;
export const HP_PER_BIOMASS = 2.0;

// --- CONVERT actions -------------------------------------------------
export const SPLIT_CHILD_FRAC = 0.4;
export const SPLIT_SCALE = 0.7;
export const GROW_MAX_SCALE = 3.0;

// --- Food -----------------------------------------------------------
export const FOOD_PLANT_BIOMASS_MIN = 4.0;
export const FOOD_PLANT_BIOMASS_MAX = 9.0;
export const FOOD_MEAT_BIOMASS_MIN = 8.0;
export const FOOD_MEAT_BIOMASS_MAX = 15.0;
export const FOOD_PLANT_FRACTION = 0.6;

// --- Creature-lab budget --------------------------------------------
export const BODY_BUDGET_BIOMASS = 80.0;
export const BODY_COST_PER_PX = 0.01;

// --- Growth tiers (1-indexed) ---------------------------------------
export const TIER_COUNT = 5;
export const TIER_THRESHOLDS = [0.0, 0.0, 40.0, 120.0, 300.0, 700.0] as const;
export const TIER_SIZE_MULTS = [1.0, 1.0, 1.25, 1.5, 1.8, 2.2] as const;

// --- Part stacks / effects (mirrors part_stats.h) -------------------
export const PART_FLAGELLA_MAX_STACK = 3;
export const PART_ARMOR_MAX_STACK = 2;
export const PART_CILIA_MAX_STACK = 3;
export const PART_REGEN_MAX_STACK = 3;
export const PART_MOUTH_MAX_STACK = 2;
export const PART_HORN_MAX_STACK = 1;
export const PART_VENOM_MAX_STACK = 2;
export const PART_EYES_MAX_STACK = 2;

export const PART_FLAGELLA_SPEED_ADD = 0.15;
export const PART_ARMOR_HP_ADD = 0.5;
export const PART_ARMOR_DR_PER = 0.15;
export const PART_SPIKE_DMG_ADD = 1.0;
export const PART_TEETH_DMG_ADD = 0.5;
export const PART_POISON_DPS = 1.0;
export const PART_POISON_RADIUS = 60.0;
export const PART_CILIA_TURN_ADD = 0.2;
export const PART_HORN_DMG_MULT = 3.0;
export const PART_HORN_DMG_SIDE = 0.15;
export const PART_HORN_CONE_COS = 0.9659258;
export const PART_REGEN_HPS = 1.5;
export const PART_MOUTH_RADIUS_ADD = 0.5;
export const PART_VENOM_DPS = 2.0;
export const PART_VENOM_DURATION = 3.0;
export const PART_EYES_PERCEPTION_ADD = 0.5;

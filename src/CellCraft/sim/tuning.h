// CellCraft — central tuning constants for the simulation.
//
// Rule 1 (CLAUDE.md): no gameplay magic numbers in engine code; every
// tunable lives here so Python mods can later override via a single
// surface. Keep this file small and self-documenting.
//
// TODO: surface these through a Python `tuning` artifact so mods can
// override without recompilation.

#pragma once

namespace civcraft::cellcraft::sim {

// --- Shape → stats ---------------------------------------------------

// turn_speed (rad/s) = TURN_K / max_core_radius, clamped to [TURN_MIN, TURN_MAX]
constexpr float TURN_K   = 120.0f;
constexpr float TURN_MIN = 0.4f;
constexpr float TURN_MAX = 6.0f;

// move_speed (world units/s) = MOVE_K / max_width, clamped
// Tuned 2026-04: MOVE_K 4000→14000 (3.5×) — prior feel was sluggish.
// MOVE_MAX/MIN bumped so small shapes can actually hit the new ceiling.
constexpr float MOVE_K   = 14000.0f;
constexpr float MOVE_MIN = 60.0f;
constexpr float MOVE_MAX = 1100.0f;

// mass = area * DENSITY (world units² × kg/unit²)
constexpr float DENSITY  = 0.02f;

// --- Shape validation ------------------------------------------------

constexpr float CLOSE_EPS       = 2.0f;   // max dist between first/last vertex to auto-close
constexpr float MIN_PERIMETER   = 40.0f;  // in world units (px)
constexpr float MAX_PERIMETER   = 2000.0f;
constexpr int   MIN_VERTS       = 3;
constexpr int   MAX_VERTS       = 200;

// --- World -----------------------------------------------------------

constexpr float DEFAULT_MAP_RADIUS = 1500.0f;
// Inward nudge when a monster strays past map_radius: v += BOUNDARY_K * (dist - R) / R toward center
constexpr float BOUNDARY_K = 600.0f;

// --- Combat / biomass ------------------------------------------------

// Damage = max(0, rel_speed_into_other * pointiness - defender_thickness) * CONTACT_K
// Tuned 2026-04: CONTACT_K 0.35→0.18, DEFENDER_THICKNESS_K 8.0→5.0, HP_PER_BIOMASS 1.0→2.0
// — was producing sub-5s matches (one-shot kills). Now matches average 60–120s.
constexpr float CONTACT_K            = 0.18f;
constexpr float DEFENDER_THICKNESS_K = 5.0f;   // subtracted from damage, scales with defender max_width
constexpr float SEPARATION_IMPULSE   = 60.0f;  // push apart on overlap to avoid sticking

// --- Part-gated damage (Spore rule: no damaging part → no damage) ----
// Damage only applies if a damaging part (SPIKE/TEETH/HORN/VENOM_SPIKE)
// on the attacker sits within PART_CONTACT_RADIUS_K * part.scale world
// units of the contact point. Base damage per connecting part type:
constexpr float PART_DMG_BASE_SPIKE       = 4.0f;
constexpr float PART_DMG_BASE_TEETH       = 3.0f;
constexpr float PART_DMG_BASE_HORN        = 6.0f;
constexpr float PART_DMG_BASE_VENOM_SPIKE = 5.0f;
// Radius (world units per unit part scale) around the part anchor in which
// a contact point counts as "connected".
constexpr float PART_CONTACT_RADIUS_K = 12.0f;
// Damage = base * part.scale * max(rel_speed * K_SPEED, K_MIN)
constexpr float PART_DMG_K_SPEED = 0.02f;
constexpr float PART_DMG_K_MIN   = 0.35f;

// --- Hard overlap resolution ----------------------------------------
// After the soft contact spring, cells are pushed apart along the contact
// normal so their polygons no longer overlap. Mass-proportional split.
// Overlap magnitude is approximated from interpenetration of bounding
// radii when polygons overlap (cheap, stable, no SAT needed).
constexpr float OVERLAP_PUSH_PAD   = 1.0f;  // extra world units past zero-overlap
constexpr float OVERLAP_MAX_PUSH   = 40.0f; // cap per-tick per-cell displacement

// Fraction of dying monster's biomass that goes to killer; remainder drops as food.
constexpr float DEATH_BIOMASS_FRAC = 0.6f;

// HP model: hp_max = biomass * HP_PER_BIOMASS; initial hp = hp_max
constexpr float HP_PER_BIOMASS = 2.0f;

// --- CONVERT actions -------------------------------------------------

// Fraction of parent biomass moved into child on split
constexpr float SPLIT_CHILD_FRAC = 0.4f;
// Child shape scale (also scales biomass cost naturally via area ∝ scale²)
constexpr float SPLIT_SCALE      = 0.7f;
// Grow: scales shape linearly; cost = (new_area - old_area) * DENSITY
constexpr float GROW_MAX_SCALE   = 3.0f;

// --- Food -----------------------------------------------------------

// Legacy generic range — kept for any remaining callers; scatter_food now
// picks per-type from the plant/meat ranges below.
constexpr float FOOD_BIOMASS_MIN = 4.0f;
constexpr float FOOD_BIOMASS_MAX = 12.0f;

// Plant: common, small. Meat: rarer, chunkier (justifies risk/reward
// once carnivore diet lands in commit 3).
constexpr float FOOD_PLANT_BIOMASS_MIN = 4.0f;
constexpr float FOOD_PLANT_BIOMASS_MAX = 9.0f;
constexpr float FOOD_MEAT_BIOMASS_MIN  = 8.0f;
constexpr float FOOD_MEAT_BIOMASS_MAX  = 15.0f;

// Fraction of scattered food that is plant (remainder is meat).
constexpr float FOOD_PLANT_FRACTION = 0.60f;

// --- Creature-lab material budget -----------------------------------

// Total biomass available when sculpting a custom monster.
constexpr float BODY_BUDGET_BIOMASS = 80.0f;
// Cost per pixel of cell boundary perimeter.
constexpr float BODY_COST_PER_PX    = 0.01f;

// --- Growth tiers (commit 5) ----------------------------------------
// Tiers are gated on *lifetime* biomass gained (monotonic, never decreases
// on damage/split), not current biomass. Each tier-up scales the body by
// the ratio of size multipliers and unlocks a new set of parts in the lab.
//
// Order matches Tier enum (1-indexed — index 0 sentinel unused).
constexpr int   TIER_COUNT             = 5;
constexpr float TIER_THRESHOLDS[6]     = { 0.0f, 0.0f, 40.0f, 120.0f, 300.0f, 700.0f };
constexpr float TIER_SIZE_MULTS[6]     = { 1.0f, 1.0f, 1.25f, 1.5f,   1.8f,   2.2f  };

} // namespace civcraft::cellcraft::sim

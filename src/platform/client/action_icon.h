#pragma once

// Tiny rect-/arc-based glyph set drawn over the focus anchor (the entity
// or block the player's crosshair is on). Replaces the older textual
// "[LMB] Attack" / "[E] Open" prompts so a glance at the screen tells
// the player what the click would do — no reading required.
//
// Kinds map 1:1 onto the executor handlers in agent/actions.h
// (MoveAction, HarvestAction, AttackAction, RelocateAction,
// InteractAction). The HUD classifies the focused target into one of
// these kinds and calls drawActionIcon(...). Move is drawn at the
// click-to-move cursor; the rest are drawn at the world anchor.

namespace solarium::rhi { class IRhi; }

namespace solarium::vk {

enum class ActionIconKind {
	Move,       // walkable ground click target
	Harvest,    // mine/chop a block
	Attack,     // strike a living entity
	Relocate,   // open a container (move items in/out)
	Interact,   // toggle a door / press a button / ignite TNT
};

// Default palette per kind — readability + genre-convention intent
// signaling. Renderer can override by passing a custom rgba.
const float* defaultIconColor(ActionIconKind kind);

// Draw one icon centered on (cx, cy) in NDC. halfSize is the icon's
// half-extent in NDC Y; the X half-extent is divided by aspect so the
// icon stays visually square. rgba is RGBA in [0..1].
void drawActionIcon(rhi::IRhi* rhi, ActionIconKind kind,
                    float cx, float cy, float halfSize, float aspect,
                    const float rgba[4]);

} // namespace solarium::vk

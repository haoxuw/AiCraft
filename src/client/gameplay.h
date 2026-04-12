#pragma once

#include "client/types.h"
#include "shared/server_interface.h"
#include "shared/entity.h"
#include "shared/constants.h"
#include "client/gl.h"
#include "client/camera.h"
#include "client/renderer.h"
#include "client/controls.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "client/entity_raycast.h"
#include "client/window.h"
#include "client/hotbar.h"
#include <optional>
#include <unordered_map>

namespace modcraft {

class AgentClient;

class GameplayController {
public:
	// Called each gameplay frame. Gathers client input → ActionProposals.
	void update(float dt, GameState state, ServerInterface& server, Entity& player,
	            const Hotbar& hotbar,
	            Camera& camera, ControlManager& controls, Renderer& renderer,
	            ParticleSystem& particles, Window& window,
	            float jumpVelocity = 8.3f);

	// --- Query state (for HUD / renderer) ---

	const std::optional<RayHit>& currentHit() const { return m_hit; }
	const std::optional<EntityHit>& currentEntityHit() const { return m_entityHit; }

	EntityId inspectedEntity() const { return m_inspectedEntity; }
	void clearInspection() { m_inspectedEntity = ENTITY_NONE; }

	// Move orders (RPG click-to-move + RTS unit movement)
	bool hasMoveTarget() const { return m_hasMoveTarget; }
	glm::vec3 moveTarget() const { return m_moveTargetPos; }

	bool isBoxDragging() const { return m_rtsSelect.dragging; }
	glm::vec2 boxStart() const { return m_rtsSelect.start; }
	glm::vec2 boxEnd() const { return m_rtsSelect.end; }
	const std::vector<EntityId>& selectedEntities() const { return m_rtsSelect.selected; }

	// Set by game loop: true when inventory/ImGui/chat is open and wants cursor
	void setUIWantsCursor(bool v) { m_uiWantsCursor = v; }

	// Injected by Game::startPlaying so click-to-move can clear the
	// target NPC's agent plan and install a "player override" synthetic
	// plan. Raw pointer — lifetime owned by Game.
	void setAgentClient(AgentClient* ac) { m_agentClient = ac; }

private:
	// --- Subsystems (each has its own .cpp implementation) ---

	void handleCameraInput(float dt, ControlManager& controls, Camera& camera, Window& window);
	void processMovement(float dt, GameState state, ControlManager& controls,
	                     Camera& camera, Entity& player, ServerInterface& server,
	                     Window& window, float jumpVelocity);
	void processBlockInteraction(float dt, GameState state, ServerInterface& server,
	                             Entity& player, const Hotbar& hotbar,
	                             Camera& camera, ControlManager& controls,
	                             Window& window);

	// --- Raycast results (set per frame by processBlockInteraction) ---

	std::optional<RayHit> m_hit;
	std::optional<EntityHit> m_entityHit;
	EntityId m_inspectedEntity = ENTITY_NONE;

	// --- Input cooldowns ---

	float m_breakCD = 0;
	bool m_uiWantsCursor = false;
	AgentClient* m_agentClient = nullptr;

	// --- Block breaking progress (survival = 3 hits) ---
public:
	bool isBreaking() const { return m_breaking.active; }
	glm::ivec3 breakTarget() const { return m_breaking.target; }
	float breakProgress() const { return m_breaking.active ? (float)m_breaking.hits / 3.0f : 0; }

	// Per-hit event (for particles/sound on each mining swing)
	struct HitEvent { bool happened = false; glm::vec3 pos; glm::vec3 color; };
	const HitEvent& hitEvent() const { return m_hitEvent; }

	// Attack swing trigger (for first-person hand animation)
	bool swingTriggered() const { return m_swingTriggered; }
	void clearSwing() { m_swingTriggered = false; }

	// Attack target: entity the player left-clicked on this frame
	EntityId attackTarget() const { return m_attackTarget; }
	void clearAttack() { m_attackTarget = ENTITY_NONE; }

	// Place event (for immediate client-side sound on block place)
	struct PlaceEvent { bool happened = false; glm::vec3 pos; std::string blockType; };
	const PlaceEvent& placeEvent() const { return m_placeEvent; }

	// Door toggle event (for door_open / door_close sound)
	bool doorToggled() const { return m_doorToggled; }
	glm::vec3 doorTogglePos() const { return m_doorTogglePos; }
	void clearDoorToggle() { m_doorToggled = false; }

	// Chest open event (set when player right-clicks a chest block)
	struct ChestOpenEvent { bool happened = false; glm::ivec3 blockPos{0,0,0}; };
	const ChestOpenEvent& chestOpenEvent() const { return m_chestOpenEvent; }
	void clearChestOpenEvent() { m_chestOpenEvent.happened = false; }

private:
	struct BreakState {
		glm::ivec3 target = {0,0,0};
		int hits = 0;
		float timer = 0;      // decays → reset if idle too long
		bool active = false;
	} m_breaking;
	HitEvent m_hitEvent;
	bool m_swingTriggered = false;
	EntityId m_attackTarget = ENTITY_NONE;
	PlaceEvent m_placeEvent;
	bool m_doorToggled = false;
	glm::vec3 m_doorTogglePos = {};
	ChestOpenEvent m_chestOpenEvent;

	// Cached cursor mode — only call glfwSetInputMode when it actually changes.
	// Calling it every frame on Emscripten re-triggers requestPointerLock() which
	// resets the internal cursor position and causes the camera to snap every frame.
	int m_cursorMode = -1; // -1 = unset (force first call)

	// --- Right-click state (RPG/RTS: drag → orbit, quick click → action) ---

	struct RightClick {
		bool held = false;
		bool orbiting = false;
		bool action = false;      // set on release if no drag
		double startX = 0, startY = 0;
	} m_rightClick;

	// --- RTS box selection + unit movement ---

	struct RTSSelection {
		bool dragging = false;
		glm::vec2 start = {0, 0};
		glm::vec2 end = {0, 0};
		std::vector<EntityId> selected;
	} m_rtsSelect;

	// --- Move orders (RPG click-to-move + RTS unit commands) ---

	struct MoveOrder {
		glm::vec3 target = {0, 0, 0};
		bool active = false;
	};
	std::unordered_map<EntityId, MoveOrder> m_moveOrders;

	// Render target highlight
	glm::vec3 m_moveTargetPos = {0, 0, 0};
	bool m_hasMoveTarget = false;
};

} // namespace modcraft

#include "client/game_vk.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <unordered_set>

#include "client/chunk_mesher.h"
#include "client/network_server.h"
#include "client/raycast.h"
#include "net/server_interface.h"
#include "logic/physics.h"
#include "logic/action.h"
#include "logic/block_shape.h"
#include "logic/material_values.h"
#include "agent/agent_client.h"

namespace civcraft::vk {

void Game::clampCameraCollision() {
	// FPS: camera inside player capsule — nothing to clamp.
	if (m_cam.mode == civcraft::CameraMode::FirstPerson) return;

	// RTS: commander view flies through walls by design; clamp would jerk it.
	if (m_cam.mode == civcraft::CameraMode::RTS) return;

	// Orbit target (TPS/RPG head anchor).
	float feetY = m_cam.smoothedFeetPos().y;
	glm::vec3 target(m_cam.player.feetPos.x,
	                 feetY + m_cam.player.eyeHeight * 0.8f,
	                 m_cam.player.feetPos.z);

	glm::vec3 delta = m_cam.position - target;
	float dist = glm::length(delta);
	if (dist < 0.05f) return;
	glm::vec3 dir = delta / dist;

	auto clampAgainst = [&](civcraft::ChunkSource& src) {
		auto hit = civcraft::raycastBlocks(src, target, dir, dist);
		if (!hit) return;
		const float kAirGap = 0.2f;
		float tHit = hit->distance;
		float pulled = std::max(0.2f, tHit - kAirGap);
		m_cam.position = target + dir * pulled;
		// Throttled log so headless runs can confirm clamp fires.
		static float sLastLog = 0;
		if (m_wallTime - sLastLog > 0.5f) {
			std::printf("[vk-game] cam-collide: pulled %.1f→%.1fb (hit @ (%d,%d,%d))\n",
			            dist, pulled, hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
			sLastLog = m_wallTime;
		}
	};
	clampAgainst(m_server->chunks());
}

void Game::processInput(float dt) {
	if (!m_window) return;

	// Mouse cursor → NDC (+Y up, matching drawRect2D) + LMB edge detection.
	// Used by custom inventory UI hit-testing and drag-and-drop. We track
	// this every frame regardless of state so the release edge fires even
	// if the inventory just closed.
	{
		double mx = 0, my = 0;
		glfwGetCursorPos(m_window, &mx, &my);
		if (m_fbW > 0 && m_fbH > 0) {
			m_mouseNdcX = (float)(mx / m_fbW) * 2.0f - 1.0f;
			m_mouseNdcY = 1.0f - (float)(my / m_fbH) * 2.0f;
		}
		bool lmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		m_mouseLPressed  = (lmb && !m_mouseLLast);
		m_mouseLReleased = (!lmb && m_mouseLLast);
		m_mouseLHeld     = lmb;
		m_mouseLLast     = lmb;
	}

	// Dialog input drain first — consumed keys are removed so the rest
	// of processInput doesn't re-trigger on them.
	drainDialogInput();

	// ESC: multi-layer dismiss (drag → dialog → inventory → pause menu).
	// Handles its own m_escLast edge detect.
	processEscapeKey();

	if (m_state != GameState::Playing) return;

	// V: cycle camera mode (FPS → TPS → RPG → RTS)
	// Selection persists across camera modes — only RMB-in-empty-space or an
	// explicit new drag-select clears it. That way a user can box-select in
	// RTS, switch to TPS to watch the action, and still own those units.
	bool v = glfwGetKey(m_window, GLFW_KEY_V) == GLFW_PRESS;
	if (v && !m_vLast) {
		m_cam.cycleMode();
		m_rtsSelect.dragging = false;
		m_cam.resetMouseTracking();
		const char* names[] = {"FPS", "TPS", "RPG", "RTS"};
		const char* name = names[(int)m_cam.mode];
		std::printf("[vk-game] camera → %s\n", name);
		// Mode name now lives in the HUD status strip (bottom-left), so the
		// transient toast would be redundant. Keep the first-time control
		// hint below — that's the only thing mode-switching teaches.
		// First entry to this mode — show key hints.
		unsigned bit = 1u << (int)m_cam.mode;
		if (!(m_modeHintsShown & bit)) {
			m_modeHintsShown |= bit;
			const char* hints[] = {
				"WASD move · mouse look · LMB attack · RMB place",
				"WASD move · mouse orbits · LMB attack · RMB place",
				"RMB-drag orbits · click ground to move · drag to box-select",
				"WASD pan · RMB-drag orbit · LMB box-select · LMB-hold=Build",
			};
			pushNotification(hints[(int)m_cam.mode],
			                 glm::vec3(0.75f, 0.82f, 0.92f), 4.0f);
		}
	}
	m_vLast = v;

	// E: interact (door/button/etc.) under cursor.
	bool eKey = glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS;
	if (eKey && !m_eLast) {
		glm::vec3 eye = m_cam.position;
		glm::vec3 dir = m_cam.front();
		if (m_cam.mode == civcraft::CameraMode::RPG ||
		    m_cam.mode == civcraft::CameraMode::RTS) {
			double mx, my;
			glfwGetCursorPos(m_window, &mx, &my);
			int ww = m_fbW, wh = m_fbH;
			if (ww > 0 && wh > 0) {
				float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
				float ndcY = 1.0f - (float)(my / wh) * 2.0f;
				glm::mat4 invVP = glm::inverse(viewProj());
				glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				dir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
			}
		}
		auto hit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);
		if (hit) {
			glm::ivec3 bp = hit->hasInteract ? hit->interactPos : hit->blockPos;

			// Chest: a UI action, not a server action — the chest Structure
			// entity's inventory already rides the normal S_INVENTORY broadcast
			// path. Find the matching entity by block position and open the
			// side-by-side inventory panel.
			auto& blocks = m_server->blockRegistry();
			const auto& bdef = blocks.get(m_server->chunks().getBlock(bp.x, bp.y, bp.z));
			if (bdef.string_id == civcraft::BlockType::Chest) {
				civcraft::EntityId chestEid = 0;
				m_server->forEachEntity([&](civcraft::Entity& e) {
					if (chestEid) return;
					if (e.typeId() != civcraft::StructureName::Chest) return;
					int ex = (int)std::floor(e.position.x);
					int ey = (int)std::floor(e.position.y);
					int ez = (int)std::floor(e.position.z);
					if (ex == bp.x && ey == bp.y && ez == bp.z) chestEid = e.id();
				});
				if (chestEid) {
					m_invOther = chestEid;
					m_invOpen  = true;
					civcraft::GameLogger::instance().emit("ACTION",
						"open chest #%u @(%d,%d,%d)", chestEid, bp.x, bp.y, bp.z);
				} else {
					civcraft::GameLogger::instance().emit("WARN",
						"chest block @(%d,%d,%d) has no Structure entity", bp.x, bp.y, bp.z);
				}
			} else {
				civcraft::ActionProposal p;
				p.type     = civcraft::ActionProposal::Interact;
				p.actorId  = m_server->localPlayerId();
				p.blockPos = bp;
				m_server->sendAction(p);
				civcraft::GameLogger::instance().emit("ACTION",
					"interact @(%d,%d,%d)", bp.x, bp.y, bp.z);
			}
		}
	}
	m_eLast = eKey;

	// F12: toggle admin mode
	bool f12 = glfwGetKey(m_window, GLFW_KEY_F12) == GLFW_PRESS;
	if (f12 && !m_f12Last) {
		m_adminMode = !m_adminMode;
		if (!m_adminMode) m_flyMode = false;
		std::printf("[vk-game] admin mode %s\n", m_adminMode ? "ON" : "OFF");
	}
	m_f12Last = f12;

	// F11: toggle fly (admin only)
	bool f11 = glfwGetKey(m_window, GLFW_KEY_F11) == GLFW_PRESS;
	if (f11 && !m_f11Last && m_adminMode) {
		m_flyMode = !m_flyMode;
		std::printf("[vk-game] fly mode %s\n", m_flyMode ? "ON" : "OFF");
	}
	m_f11Last = f11;

	// F3: toggle debug overlay
	bool f3 = glfwGetKey(m_window, GLFW_KEY_F3) == GLFW_PRESS;
	if (f3 && !m_f3Last) m_showDebug = !m_showDebug;
	m_f3Last = f3;

	// F6: render-tuning panel.
	bool f6 = glfwGetKey(m_window, GLFW_KEY_F6) == GLFW_PRESS;
	if (f6 && !m_f6Last) m_showTuning = !m_showTuning;
	m_f6Last = f6;

	// H: handbook (artifact browser).
	bool hKey = glfwGetKey(m_window, GLFW_KEY_H) == GLFW_PRESS;
	if (hKey && !m_hLast)
		m_handbookOpen = !m_handbookOpen;
	m_hLast = hKey;

	// T: talk to humanoid NPC under cursor / crosshair.
	processTalkKey();

	// Tab: toggle inventory.
	bool tabKey = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
	if (tabKey && !m_tabLast)
		m_invOpen = !m_invOpen;
	m_tabLast = tabKey;

	// R: rotate held block. MMB click also cycles (see LMB/MMB section
	// below); the two bindings share cyclePlacementRotation().
	bool rKey = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
	if (rKey && !m_rKeyLast)
		cyclePlacementRotation();
	m_rKeyLast = rKey;

	// 1..9, 0: select hotbar slot. 0 is the rightmost slot (index 9).
	{
		int picked = -1;
		for (int i = 0; i < 9; i++) {
			if (glfwGetKey(m_window, GLFW_KEY_1 + i) == GLFW_PRESS) {
				picked = i; break;
			}
		}
		if (picked < 0 && glfwGetKey(m_window, GLFW_KEY_0) == GLFW_PRESS)
			picked = 9;
		if (picked >= 0 && picked != m_hotbar.selected) {
			m_hotbar.selected = picked;
			if (!m_hotbarSavePath.empty())
				m_hotbar.saveToFile(m_hotbarSavePath);
		}
	}

	// Reset placement rotation whenever the active hotbar slot changes
	// so switching between blocks doesn't inherit a stale param2.
	if (m_hotbar.selected != m_placementHotbarSlot) {
		m_placementParam2     = 0;
		m_placementHotbarSlot = m_hotbar.selected;
	}

	// Q: drop one of the currently held item (hotbar selection). Uses
	// TYPE_RELOCATE (Self → Ground) — no new action type needed.
	bool qKey = glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS;
	if (qKey && !m_qLast) {
		civcraft::Entity* me = playerEntity();
		if (me && me->inventory) {
			const std::string& held = m_hotbar.mainHand(*me->inventory);
			if (!held.empty()) {
				civcraft::ActionProposal p;
				p.type        = civcraft::ActionProposal::Relocate;
				p.actorId     = m_server->localPlayerId();
				p.relocateFrom = civcraft::Container::self();
				p.relocateTo   = civcraft::Container::ground();
				p.itemId      = held;
				p.itemCount   = 1;
				m_server->sendAction(p);
				civcraft::GameLogger::instance().emit("ACTION",
					"drop %s x1", held.c_str());
			}
		}
	}
	m_qLast = qKey;

	// ── Cursor mode ───────────────────────────────────────────────────────
	// FPS/TPS: cursor captured for mouse look.
	// RPG/RTS: cursor free; right-click-drag = orbit camera.
	// UI overlays (handbook, inspector, tuning) always show cursor.
	m_uiWantsCursor = m_handbookOpen || m_inspectedEntity != 0 || m_showTuning
	                || m_invOpen || m_dialogPanel.isOpen();

	bool wantCapture = (m_cam.mode == civcraft::CameraMode::FirstPerson ||
	                    m_cam.mode == civcraft::CameraMode::ThirdPerson);

	if (m_cam.mode == civcraft::CameraMode::RPG ||
	    m_cam.mode == civcraft::CameraMode::RTS) {
		bool rmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		if (rmb && !m_rightClick.held) {
			m_rightClick.held = true;
			m_rightClick.orbiting = false;
			glfwGetCursorPos(m_window, &m_rightClick.startX, &m_rightClick.startY);
		}
		// RTS drag-command steals RMB-drag from camera orbit when a selection
		// exists: selected units = command mode; empty selection = camera mode.
		bool commandDrag = (m_cam.mode == civcraft::CameraMode::RTS
		                    && !m_rtsSelect.selected.empty());
		if (rmb && m_rightClick.held && !m_rightClick.orbiting && !commandDrag) {
			double cx, cy;
			glfwGetCursorPos(m_window, &cx, &cy);
			double ddx = cx - m_rightClick.startX, ddy = cy - m_rightClick.startY;
			if (ddx * ddx + ddy * ddy > 25.0) {
				m_rightClick.orbiting = true;
				m_cam.resetMouseTracking();
			}
		}
		if (!rmb && m_rightClick.held) {
			m_rightClick.held = false;
			if (!m_rightClick.orbiting)
				m_rightClick.action = true;
			m_rightClick.orbiting = false;
		}
		wantCapture = m_rightClick.orbiting;
	}

	if (m_uiWantsCursor)
		wantCapture = false;

	int newCursorMode = wantCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
	bool wasCaptured = m_mouseCaptured;
	m_mouseCaptured = wantCapture;
	if ((wantCapture ? 1 : 0) != (wasCaptured ? 1 : 0)) {
		glfwSetInputMode(m_window, GLFW_CURSOR, newCursorMode);
		m_cam.resetMouseTracking();
	}

	// Sync player pos → camera before input.
	if (auto* me = playerEntity())
		m_cam.player.feetPos = me->position;

	if (wantCapture) {
		m_cam.processInput(m_window, dt);
	} else {
		m_cam.resetMouseTracking();
		switch (m_cam.mode) {
		case civcraft::CameraMode::RPG: m_cam.updateRPGPosition(dt); break;
		case civcraft::CameraMode::RTS: m_cam.updateRTS(m_window, dt); break;
		default: break;
		}
	}

	clampCameraCollision();
}

void Game::tickPlayer(float dt) {
	if (!m_window) return;
	civcraft::Entity* me = playerEntity();
	if (!me) return;

	// RTS: WASD pans camera, not player. Player walks only when box-selected
	// + issued a group Move order (Rule 2). Local steering uses steerTargetFor
	// over the client-prediction path.
	bool rtsMode = (m_cam.mode == civcraft::CameraMode::RTS);

	glm::vec3 fwd, right;
	if (m_cam.mode == civcraft::CameraMode::RPG) {
		fwd   = m_cam.godCameraForward();
		right = m_cam.godCameraRight();
	} else {
		fwd   = playerForward();
		right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
	}
	glm::vec3 mv(0);
	if (!rtsMode) {
		if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) mv += fwd;
		if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) mv -= fwd;
		if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) mv -= right;
		if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) mv += right;
	} else {
		civcraft::EntityId pid = m_server->localPlayerId();
		auto pit = m_moveOrders.find(pid);
		if (pit != m_moveOrders.end() && pit->second.active) {
			auto wp = m_rtsExec.steerTargetFor(pid, me->position);
			glm::vec3 steer = wp ? *wp : pit->second.target;
			glm::vec3 toTarget = steer - me->position;
			toTarget.y = 0;
			float dist = glm::length(toTarget);
			glm::vec3 finalDelta = pit->second.target - me->position;
			finalDelta.y = 0;
			if (glm::length(finalDelta) < 1.0f) {
				m_moveOrders.erase(pit);
				m_rtsExec.cancel(pid);
			} else if (dist > 0.01f) {
				mv = toTarget / dist;
			}
		}
	}

	float moveLen = glm::length(mv);
	glm::vec3 moveDir = moveLen > 0.001f ? mv / moveLen : glm::vec3(0);
	bool boost = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

	// Sprint FOV punch — FPS/TPS only (RPG/RTS keep fixed FOV).
	{
		bool sprinting = boost && moveLen > 0.001f &&
		    (m_cam.mode == civcraft::CameraMode::FirstPerson ||
		     m_cam.mode == civcraft::CameraMode::ThirdPerson);
		const float kTarget = sprinting ? 6.0f : 0.0f;
		// up 7/s (~0.15s), down 4/s (~0.25s).
		float rate = kTarget > m_sprintFovBoost ? 7.0f : 4.0f;
		m_sprintFovBoost += (kTarget - m_sprintFovBoost) * std::min(rate * dt, 1.0f);
		m_cam.fov = 70.0f + m_sprintFovBoost;
	}

	// Click-to-move virtual joystick. Server runs parallel pathfinder; WASD
	// cancels; arriving within 1.5b clears.
	if (m_hasMoveOrder) {
		if (moveLen > 0.001f) {
			std::printf("[vk-game] click-to-move: canceled by WASD @ (%.1f,%.1f,%.1f)\n",
			            me->position.x, me->position.y, me->position.z);
			m_hasMoveOrder = false;
		} else {
			glm::vec3 toTarget = m_moveOrderTarget - me->position;
			toTarget.y = 0;
			float d = glm::length(toTarget);
			static float sLastLog = 0;
			if (m_wallTime - sLastLog > 0.5f) {
				std::printf("[vk-game] click-to-move: pos=(%.1f,%.1f,%.1f) d=%.1f\n",
				            me->position.x, me->position.y, me->position.z, d);
				sLastLog = m_wallTime;
			}
			if (d < 1.5f) {
				std::printf("[vk-game] click-to-move: arrived @ (%.1f,%.1f,%.1f)\n",
				            me->position.x, me->position.y, me->position.z);
				m_hasMoveOrder = false;
			} else {
				moveDir = toTarget / d;
				moveLen = 1.0f;
			}
		}
	}

	bool space = glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS;
	m_spaceLast = space;

	// Horizontal velocity: direct (arcade feel).
	float speed = kTune.playerSpeed * (boost ? 1.6f : 1.0f);

	{
		auto& chunks = m_server->chunks();
		auto& blocks = m_server->blockRegistry();
		civcraft::BlockSolidFn isSolid = [&](int x, int y, int z) -> float {
			const auto& bd = blocks.get(chunks.getBlock(x, y, z));
			return bd.solid ? bd.collision_height : 0.0f;
		};

		// MoveParams from EntityDef — must match server's makeMoveParams.
		const auto& def = me->def();
		civcraft::MoveParams mp = civcraft::makeMoveParams(
			def.collision_box_min, def.collision_box_max,
			def.gravity_scale, def.isLiving(), m_flyMode);
		// Cap ground-snap to slab height so 1-block drops fall with gravity
		// but half-slabs still step down smoothly. NPCs keep default (2.0).
		mp.maxGroundSnap = 0.4f;

		int fx = (int)std::floor(me->position.x);
		int fy = (int)std::floor(me->position.y) - 1;
		int fz = (int)std::floor(me->position.z);
		civcraft::ChunkPos fp = { fx >> 4, fy >> 4, fz >> 4 };
		bool feetReady = chunks.getChunkIfLoaded(fp) != nullptr;

		// Build Move proposal (horizontal from input, Y filled later).
		civcraft::ActionProposal a;
		a.type         = civcraft::ActionProposal::Move;
		a.actorId      = m_server->localPlayerId();
		a.desiredVel   = { moveDir.x * speed, 0, moveDir.z * speed };
		a.sprint       = boost;
		a.jumpVelocity = kTune.playerJumpV;
		a.fly          = m_flyMode;
		a.lookYaw      = m_cam.lookYaw;
		a.lookPitch    = m_cam.lookPitch;

		if (m_flyMode) {
			if (space) a.desiredVel.y = speed;
			else if (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) a.desiredVel.y = -speed;
		} else {
			a.jump = space;
		}

		if (!feetReady) {
			// Ground chunk not loaded — freeze + inform server.
			me->velocity = glm::vec3(0);
			a.clientPos    = me->position;
			a.hasClientPos = true;
			a.desiredVel   = glm::vec3(0);
			m_server->sendAction(a);
		} else {
			// Client physics: same moveAndCollide as server.
			glm::vec3 localVel = { a.desiredVel.x, me->velocity.y, a.desiredVel.z };
			if (m_flyMode) {
				localVel.y = a.desiredVel.y;
			} else if (a.jump && m_onGround) {
				localVel.y = kTune.playerJumpV;
			}

			glm::vec3 prePos = me->position;
			glm::vec3 preVel = me->velocity;
			bool preOnGround = m_onGround;
			auto r = civcraft::moveAndCollide(isSolid, me->position, localVel, dt, mp, m_onGround);
			me->position = r.position;
			me->velocity = r.velocity;
			m_onGround   = r.onGround;

			// Ledge-step: cosmetic climb anim so the eye sees a rise
			// instead of a teleport. Duration scales 0.22s/block.
			if (r.stepped) {
				float dy = r.position.y - prePos.y;
				m_climb.t        = 0.0f;
				m_climb.duration = std::max(0.12f, dy * 0.22f);
				m_climb.fromY    = prePos.y;
				m_climb.toY      = r.position.y;
				glm::vec2 hv(r.position.x - prePos.x, r.position.z - prePos.z);
				float hvLen = glm::length(hv);
				m_climb.forward = hvLen > 0.001f ? hv / hvLen
				                                 : glm::vec2(std::cos(glm::radians(m_cam.lookYaw)),
				                                             std::sin(glm::radians(m_cam.lookYaw)));
			}

			// If stuck in a block, revert so server runs auth physics this tick.
			bool clientPosInvalid = civcraft::isPositionBlocked(
				isSolid, me->position, mp.halfWidth, mp.height);
			if (clientPosInvalid) {
				me->position = prePos;
				me->velocity = preVel;
				m_onGround   = preOnGround;
			}

			a.clientPos    = me->position;
			a.hasClientPos = !clientPosInvalid;
			a.desiredVel.y = me->velocity.y;
			m_server->sendAction(a);
		}

		// entity.position IS the predicted position; S_ENTITY snaps on drift.

		if (me->hp() <= 0 && m_state == GameState::Playing)
			enterDead("You died.");

		// Damage resets regen timer.
		float serverHp = (float)me->hp();
		static float sLastHp = -1;
		if (sLastHp >= 0 && serverHp < sLastHp) m_regenIdle = 0;
		sLastHp = serverHp;
	}

	// Walk dist drives arm/leg swing.
	if (m_onGround && moveLen > 0.001f)
		m_walkDist += speed * dt;

	// Body yaw. FPS: snap to camera. TPS/RPG/RTS: lerp to velocity direction
	// so character faces the way they're moving, not the camera.
	{
		float targetYaw;
		bool haveTarget = true;
		if (m_cam.mode == civcraft::CameraMode::FirstPerson) {
			targetYaw = glm::radians(m_cam.lookYaw);
		} else {
			glm::vec2 hv(me->velocity.x, me->velocity.z);
			if (glm::length(hv) > 0.3f) {
				targetYaw = std::atan2(hv.y, hv.x);
			} else {
				haveTarget = m_playerBodyYawInit;
				targetYaw = m_playerBodyYaw;
			}
		}
		if (!m_playerBodyYawInit) {
			m_playerBodyYaw = targetYaw;
			m_playerBodyYawInit = true;
		} else if (haveTarget) {
			// Shortest-arc lerp @ 8/s.
			float diff = targetYaw - m_playerBodyYaw;
			while (diff >  3.1415926535f) diff -= 6.2831853072f;
			while (diff < -3.1415926535f) diff += 6.2831853072f;
			float k = std::min(8.0f * dt, 1.0f);
			m_playerBodyYaw += diff * k;
		}
	}

	// Footstep audio on integer stride crossing (~2 blocks per step).
	if (m_footstepCooldown > 0) m_footstepCooldown -= dt;
	{
		int stepIdx = (int)(m_walkDist * 0.5f);   // ~1 step / 2 blocks
		bool walking = m_onGround && moveLen > 0.001f;
		if (walking && stepIdx != m_lastWalkStep && m_footstepCooldown <= 0) {
			m_lastWalkStep = stepIdx;
			m_footstepCooldown = 0.25f;
			// Pick step sound from the block underfoot.
			int fx = (int)std::floor(me->position.x);
			int fy = (int)std::floor(me->position.y) - 1;
			int fz = (int)std::floor(me->position.z);
			civcraft::BlockId bid = m_server->chunks().getBlock(fx, fy, fz);
			const auto& bdef = m_server->blockRegistry().get(bid);
			std::string snd = bdef.sound_footstep;
			if (snd.empty()) {
				const std::string& sid = bdef.string_id;
				if      (sid.find("grass") != std::string::npos) snd = "step_grass";
				else if (sid.find("wood")  != std::string::npos ||
				         sid.find("log")   != std::string::npos ||
				         sid.find("plank") != std::string::npos) snd = "step_wood";
				else if (sid.find("stone") != std::string::npos ||
				         sid.find("cobble")!= std::string::npos) snd = "step_stone";
				else if (sid.find("sand")  != std::string::npos) snd = "step_sand";
				else if (sid.find("snow")  != std::string::npos ||
				         sid.find("ice")   != std::string::npos) snd = "step_snow";
				else snd = "step_dirt";
			}
			m_audio.play(snd, me->position, 0.35f);
		}
		if (!walking) m_lastWalkStep = stepIdx;  // reset phase on stop
	}

	// Listener follows camera for correct panning.
	m_audio.setListener(m_cam.position, m_cam.front());
	m_audio.updateMusic();

	// HP regen (prediction only — server authoritative).
	m_regenIdle += dt;

	if (m_attackCD > 0) m_attackCD -= dt;
	if (m_breakCD > 0) m_breakCD -= dt;
	if (m_placeCD > 0) m_placeCD -= dt;
	if (m_climb.active()) m_climb.t += dt;
	if (m_handSwingT >= 0.0f) {
		m_handSwingT += dt;
		if (m_handSwingT > kHandSwingDur) m_handSwingT = -1.0f;
	}
	// Damage FX decay: vignette ~0.5s, shake linear.
	if (m_damageVignette > 0.0f) {
		m_damageVignette -= dt * 2.0f;
		if (m_damageVignette < 0.0f) m_damageVignette = 0.0f;
	}
	if (m_cameraShake > 0.0f) {
		m_cameraShake -= dt;
		if (m_cameraShake < 0.0f) { m_cameraShake = 0.0f; m_shakeIntensity = 0.0f; }
	}

	// Break timeout: cancel after 2s idle.
	if (m_breaking.active) {
		m_breaking.timer += dt;
		if (m_breaking.timer > 2.0f) {
			m_breaking.active = false;
			m_breaking.hits = 0;
		}
	}

	// Mouse-button event handlers — each is multi-hundred lines.
	processLmbInput(dt);
	processRmbInput(dt);
	processMmbInput();
}

void Game::digInFront() {
	if (!m_rhi) return;
	glm::vec3 eye = m_cam.position;
	glm::vec3 dir = m_cam.front();
	if (m_cam.mode == civcraft::CameraMode::RPG ||
	    m_cam.mode == civcraft::CameraMode::RTS) {
		double mx, my;
		glfwGetCursorPos(m_window, &mx, &my);
		int ww = m_fbW, wh = m_fbH;
		if (ww > 0 && wh > 0) {
			float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
			float ndcY = 1.0f - (float)(my / wh) * 2.0f;
			glm::mat4 invVP = glm::inverse(viewProj());
			glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
			glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
			dir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
		}
	}
	auto& chunks = m_server->chunks();
	auto hit = civcraft::raycastBlocks(chunks, eye, dir, 16.0f);
	if (!hit) return;
	const auto& reg = m_server->blockRegistry();
	const auto& bdef = reg.get(hit->blockId);
	civcraft::ActionProposal p;
	p.actorId     = m_server->localPlayerId();
	p.type        = civcraft::ActionProposal::Convert;
	p.fromItem    = bdef.string_id;
	p.toItem      = bdef.drop.empty() ? bdef.string_id : bdef.drop;
	p.fromCount   = 1;
	p.toCount     = 1;
	p.convertFrom = civcraft::Container::block(hit->blockPos);
	p.convertInto = civcraft::Container::ground();
	m_server->sendAction(p);

	// Predict: snap to AIR in LocalWorld, fire VFX callback. Break burst +
	// sound + floater live in the callback so villager/TNT breaks (S_BLOCK
	// only) share the same path.
	//
	// TODO(reject snap-back): we stay optimistic on reject. The only
	// reachable Convert reject for a player break is SourceBlockGone —
	// someone else broke it first, so predicted AIR is correct anyway.
	m_server->predictBlockBreak(hit->blockPos);
	syncRemeshBlock(hit->blockPos);
}

// Shared by predictBlockBreak / predictBlockPlace. Same-frame rebuild of
// the target chunk so the optimistic edit is visible on the click frame,
// not 1–3 frames later. See the method comment in game_vk.h.
void Game::syncRemeshBlock(glm::ivec3 wpos) {
	auto divDown = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
	const int CS = civcraft::CHUNK_SIZE;
	civcraft::ChunkPos cp = {
		divDown(wpos.x, CS),
		divDown(wpos.y, CS),
		divDown(wpos.z, CS)
	};
	civcraft::ChunkMesher::PaddedSnapshot snap;
	if (!civcraft::ChunkMesher::snapshotPadded(m_server->chunks(), cp, snap)) return;
	auto built = civcraft::ChunkMesher::buildMeshFromSnapshot(
		snap, cp, m_server->blockRegistry());
	if (m_inFlightMesh.count(cp)) m_staleInflightMeshes.insert(cp);
	civcraft::AsyncChunkMesher::Result r{cp, std::move(built.first), std::move(built.second)};
	applyMeshResult(std::move(r));
	m_serverDirtyChunks.erase(cp);
}

void Game::placeBlock() {
	glm::vec3 eye = m_cam.position;
	glm::vec3 dir = m_cam.front();
	if (m_cam.mode == civcraft::CameraMode::RPG ||
	    m_cam.mode == civcraft::CameraMode::RTS) {
		double mx, my;
		glfwGetCursorPos(m_window, &mx, &my);
		int ww = m_fbW, wh = m_fbH;
		if (ww > 0 && wh > 0) {
			float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
			float ndcY = 1.0f - (float)(my / wh) * 2.0f;
			glm::mat4 invVP = glm::inverse(viewProj());
			glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
			glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
			dir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
		}
	}
	auto& chunks = m_server->chunks();
	auto hit = civcraft::raycastBlocks(chunks, eye, dir, 8.0f);
	if (!hit) return;

	auto* me = playerEntity();
	if (!me || !me->inventory) return;

	// Held item drives placement: its id must be a registered block type and
	// the player must still carry one. Items with no matching block (sword,
	// potion) naturally no-op — the registry lookup fails.
	const std::string& blockType = m_hotbar.mainHand(*me->inventory);
	if (blockType.empty()) return;
	const civcraft::BlockDef* placedDef = m_server->blockRegistry().find(blockType);
	if (!placedDef) return;

	civcraft::ActionProposal p;
	p.actorId     = m_server->localPlayerId();
	p.type        = civcraft::ActionProposal::Convert;
	p.fromItem    = blockType;
	p.toItem      = blockType;
	p.fromCount   = 1;
	p.toCount     = 1;
	p.convertInto = civcraft::Container::block(hit->placePos);
	// R key / MMB click cycles m_placementParam2 for rotatable blocks;
	// non-rotatable ones always ship 0. Server honors it for FourDir
	// rotatable blocks; doors ignore it and auto-hinge from neighbors.
	p.placeParam2 = placementParam2ForHeld(*placedDef);
	m_server->sendAction(p);
	civcraft::GameLogger::instance().emit("ACTION", "placed %s @(%d,%d,%d) p2=%u",
		blockType.c_str(), hit->placePos.x, hit->placePos.y, hit->placePos.z,
		(unsigned)p.placeParam2);

	// Client-side prediction — mirror of predictBlockBreak. Writes the
	// placed block to LocalWorld and decrements inventory locally so the
	// player sees the block + the new hotbar count on the same frame as
	// the click. Server's Convert reject path re-emits onBlockChange for
	// the target cell (see server.cpp resolveActions), so if this place
	// is rejected the follow-up S_BLOCK snaps LocalWorld back to whatever
	// the server actually has.
	//
	// Doors get a ~1 round-trip hinge flicker since the server picks the
	// hinge from neighbor walls and we predict 0 — not a correctness issue.
	civcraft::BlockId bid = m_server->blockRegistry().getId(blockType);
	uint8_t predictP2 = (placedDef->mesh_type != civcraft::MeshType::Door)
		? p.placeParam2 : 0;
	m_server->predictBlockPlace(hit->placePos, bid, predictP2, /*appearance=*/0);
	if (me->inventory) me->inventory->remove(blockType, 1);
	syncRemeshBlock(hit->placePos);
}

// Fetch the BlockDef for the currently-held hotbar item, or null if
// the hotbar is empty / the item isn't a registered block. Shared by
// rotation helpers so the hotbar lookup isn't open-coded three times.
const civcraft::BlockDef* Game::heldBlockDef() {
	auto* me = playerEntity();
	if (!me || !me->inventory) return nullptr;
	const std::string& held = m_hotbar.mainHand(*me->inventory);
	if (held.empty()) return nullptr;
	return m_server->blockRegistry().find(held);
}

bool Game::isHeldBlockRotatable() {
	const civcraft::BlockDef* def = heldBlockDef();
	if (!def) return false;
	return civcraft::getBlockShape(def->mesh_type).rotationCount() > 1;
}

void Game::cyclePlacementRotation() {
	const civcraft::BlockDef* def = heldBlockDef();
	if (!def) return;
	int n = civcraft::getBlockShape(def->mesh_type).rotationCount();
	if (n <= 1) return;
	m_placementParam2 = (uint8_t)((m_placementParam2 + 1) % n);
}

uint8_t Game::placementParam2ForHeld(const civcraft::BlockDef& def) const {
	int n = civcraft::getBlockShape(def.mesh_type).rotationCount();
	if (n <= 1) return 0;
	return (uint8_t)(m_placementParam2 % n);
}

void Game::clickToMove() {
	// RPG/RTS use screen cursor, not camera forward.
	double mx = 0, my = 0;
	glfwGetCursorPos(m_window, &mx, &my);
	int ww = m_fbW, wh = m_fbH;
	if (ww <= 0 || wh <= 0) return;
	float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
	float ndcY = 1.0f - (float)(my / wh) * 2.0f;

	// Near→far cursor ray. Use non-Y-flipped proj so +y=up unprojection works.
	glm::mat4 proj = m_cam.projectionMatrix(m_aspect);  // no Y-flip
	glm::mat4 invVP = glm::inverse(proj * m_cam.viewMatrix());
	glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
	glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
	near4 /= near4.w; far4 /= far4.w;
	glm::vec3 rayOrigin(near4);
	glm::vec3 rayDir = glm::normalize(glm::vec3(far4) - rayOrigin);

	auto& chunks = m_server->chunks();
	auto hit = civcraft::raycastBlocks(chunks, rayOrigin, rayDir, 200.0f);
	if (!hit) return;

	// Goal on top of hit block (y+1 is walkable).
	glm::vec3 target = glm::vec3(hit->blockPos) + glm::vec3(0.5f, 1.0f, 0.5f);

	// Only walk where a 2-block body fits above solid ground.
	const auto& reg = m_server->blockRegistry();
	auto solid = [&](int x, int y, int z) {
		return reg.get(chunks.getBlock(x, y, z)).solid;
	};
	if (!solid(hit->blockPos.x, hit->blockPos.y, hit->blockPos.z)) return;
	if (solid(hit->blockPos.x, hit->blockPos.y + 1, hit->blockPos.z)) return;
	if (solid(hit->blockPos.x, hit->blockPos.y + 2, hit->blockPos.z)) return;

	std::printf("[vk-game] click-to-move → (%.1f, %.1f, %.1f)\n",
	            target.x, target.y, target.z);
	// Direct-drive: virtual joystick loop (drivePlayerTick) steers toward
	// m_moveOrderTarget, client-side physics + reconciliation handle the rest.
	// No server RPC — nav is client-only now (civcraft_engine.Navigator).
	m_hasMoveOrder    = true;
	m_moveOrderTarget = target;
}

bool Game::tryServerAttack() {
	auto* me = playerEntity();
	if (!me) return false;
	glm::vec3 from = me->position + glm::vec3(0, kTune.playerHeight * 0.6f, 0);
	glm::vec3 fwd  = playerForward();
	civcraft::EntityId myId = m_server->localPlayerId();

	// Swing whoosh fires on every attempt; hit sound comes from HP-delta
	// detector (Rule 5: effects derived from broadcast stream).
	m_audio.play("sword_swing", me->position, 0.5f);

	civcraft::Entity* best = nullptr;
	float bestDist = kTune.attackRange + 0.01f;
	m_server->forEachEntity([&](civcraft::Entity& e) {
		if (e.id() == myId || e.removed || !e.alive()) return;
		glm::vec3 toXZ = e.position - from;
		toXZ.y = 0;
		float d = glm::length(toXZ);
		if (d > kTune.attackRange || d < 0.01f) return;
		float cosA = glm::dot(toXZ / d, glm::vec3(fwd.x, 0, fwd.z));
		if (cosA < std::cos(kTune.attackCone)) return;
		if (d < bestDist) { bestDist = d; best = &e; }
	});
	if (!best) return false;

	civcraft::ActionProposal p;
	p.actorId     = myId;
	p.type        = civcraft::ActionProposal::Convert;
	p.convertFrom = civcraft::Container::entity(best->id());
	p.fromItem    = "hp";
	p.fromCount   = kTune.attackDmg;
	p.toItem      = "";
	m_server->sendAction(p);

	m_floaters.push_back({
		best->position + glm::vec3(0, 2.4f, 0),
		{1.0f, 0.85f, 0.30f},
		"-" + std::to_string(kTune.attackDmg),
		0.0f, 1.0f, 1.6f
	});

	// Hitmarker flash: orange hit, red kill.
	m_hitmarkerTimer = 0.18f;
	m_hitmarkerKill  = (best->hp() <= kTune.attackDmg);

	std::printf("[vk-game] swing → server Convert hp -%d on %s#%u\n",
		kTune.attackDmg, best->typeId().c_str(), best->id());
	return true;
}

void Game::tickCombat(float dt) {
	for (auto& s : m_slashes) s.t += dt;
	m_slashes.erase(
		std::remove_if(m_slashes.begin(), m_slashes.end(),
			[](const Slash& s){ return s.t >= s.duration; }),
		m_slashes.end());

	if (m_hitmarkerTimer > 0) m_hitmarkerTimer -= dt;

	// Block-break debris: integrate gravity + drag per part, cull the burst
	// once its lifetime is up. Drag (0.88^frame at 60Hz) keeps the spray
	// from flying unrealistically far; we skip floor collision — debris
	// passing through the ground looks fine at this scale and saves raycasts.
	constexpr float kGravity = 12.0f;  // m/s² (heavier than real g for punchy feel)
	constexpr float kDrag    = 0.88f;
	float dragStep = std::pow(kDrag, dt * 60.0f);
	for (auto& b : m_breakBursts) {
		b.t += dt;
		for (auto& p : b.parts) {
			p.vel.y -= kGravity * dt;
			p.vel   *= dragStep;
			p.pos   += p.vel * dt;
		}
	}
	m_breakBursts.erase(
		std::remove_if(m_breakBursts.begin(), m_breakBursts.end(),
			[](const BreakBurst& b){ return b.t >= BreakBurst::kDuration; }),
		m_breakBursts.end());
}

void Game::spawnBreakBurst(glm::vec3 center, glm::vec3 color) {
	BreakBurst b;
	// Darken the block's surface color for debris — raw color_top is often
	// so close to the surrounding terrain that chunks blend in. Mixing 45%
	// toward black reads clearly as "broken chunks" against any backdrop.
	b.color  = color * 0.45f;
	b.origin = glm::floor(center - glm::vec3(0.5f));  // block's min corner
	b.t      = 0.0f;
	// 14 small cubes sprayed from the block center with a mostly-upward,
	// mostly-outward initial velocity. Deterministic pseudo-random seed
	// from the center coord keeps two bursts at the same block identical —
	// irrelevant visually since they despawn fast, but trivial to implement.
	constexpr int kParts = 14;
	b.parts.resize(kParts);
	float seed = center.x * 31.1f + center.y * 17.7f + center.z * 13.3f;
	for (int i = 0; i < kParts; i++) {
		float a = seed + (float)i * 2.399f;
		float dx = std::sin(a * 1.37f);
		float dz = std::cos(a * 1.91f);
		float dy = 0.55f + 0.35f * std::sin(a * 0.71f);
		// Jitter spawn slightly inside the block so the cloud looks volumetric,
		// not like it spawned from a single point.
		glm::vec3 jitter = glm::vec3(std::sin(a*3.1f), std::cos(a*2.7f), std::sin(a*4.3f)) * 0.18f;
		b.parts[i].pos  = center + jitter;
		b.parts[i].vel  = glm::vec3(dx, dy, dz) * (1.6f + 0.8f * std::sin(a*5.1f));
		b.parts[i].size = 0.10f + 0.04f * std::cos(a * 7.3f);
	}
	m_breakBursts.push_back(std::move(b));
}

void Game::tickFloaters(float dt) {
	for (auto& f : m_floaters) f.t += dt;
	m_floaters.erase(
		std::remove_if(m_floaters.begin(), m_floaters.end(),
			[](const FloatText& f){ return f.t >= f.lifetime; }),
		m_floaters.end());

	for (auto& a : m_doorAnims) a.timer += dt;
	m_doorAnims.erase(
		std::remove_if(m_doorAnims.begin(), m_doorAnims.end(),
			[](const DoorAnim& a){ return a.timer >= 0.25f; }),
		m_doorAnims.end());

	for (auto& n : m_notifs) n.t += dt;
	m_notifs.erase(
		std::remove_if(m_notifs.begin(), m_notifs.end(),
			[](const Notification& n){ return n.t >= n.lifetime; }),
		m_notifs.end());
}

// Auto-pickup: one Relocate per item per kPickupCooldown; fly anim + "+N"
// floater fire only when the server removes the item (accepted). On no-show
// within kPickupWait, emit one "Pickup denied" floater and hold the cooldown
// so we don't re-send.
void Game::updatePickups(float dt) {
	civcraft::Entity* pe = playerEntity();
	if (!pe || !m_server) return;
	const auto& pdef = pe->def();
	if (pdef.pickup_range <= 0.0f) return;

	// Advance running fly anims; emit "+N item_name" + sound on completion.
	for (auto it = m_pickupAnims.begin(); it != m_pickupAnims.end(); ) {
		it->t += dt;
		if (it->t >= it->duration) {
			FloatText ft;
			std::string key = it->itemType;
			auto col = key.find(':');
			if (col != std::string::npos) key = key.substr(col + 1);
			if (!key.empty()) key[0] = (char)std::toupper((unsigned char)key[0]);
			for (auto& c : key) if (c == '_') c = ' ';
			ft.worldPos = pe->position + glm::vec3(0, 2.0f, 0);
			ft.color    = glm::vec3(0.55f, 0.95f, 0.55f);
			ft.text     = "+" + std::to_string(it->count) + " " + key;
			ft.lifetime = 1.0f;
			m_floaters.push_back(ft);
			m_audio.play("item_pickup", 0.5f);
			it = m_pickupAnims.erase(it);
		} else ++it;
	}

	// Process outstanding requests: server approval → fly anim; kPickupWait
	// with entity still present → one denial floater; kPickupCooldown → expire.
	for (auto it = m_pickupRequests.begin(); it != m_pickupRequests.end(); ) {
		it->second.age += dt;
		civcraft::EntityId eid = it->first;
		civcraft::Entity*  check = m_server->getEntity(eid);
		bool gone = !check || check->removed;

		if (gone && !it->second.deniedShown) {
			PickupAnim a;
			a.itemId   = eid;
			a.startPos = it->second.startPos;
			a.color    = it->second.color;
			a.itemType = it->second.itemType;
			a.count    = it->second.count;
			a.duration = pdef.pickup_fly_duration;
			m_pickupAnims.push_back(a);
			it = m_pickupRequests.erase(it);
			continue;
		}
		if (!it->second.deniedShown && it->second.age >= kPickupWait) {
			FloatText ft;
			ft.worldPos = pe->position + glm::vec3(0, 2.0f, 0);
			ft.color    = glm::vec3(0.95f, 0.45f, 0.45f);
			ft.text     = "Pickup denied";
			ft.lifetime = 1.0f;
			m_floaters.push_back(ft);
			it->second.deniedShown = true;
		}
		if (it->second.age >= kPickupCooldown) {
			it = m_pickupRequests.erase(it);
		} else ++it;
	}

	// Scan for new in-range items and send one Relocate each.
	auto hasAnim = [this](civcraft::EntityId eid) {
		for (const auto& a : m_pickupAnims) if (a.itemId == eid) return true;
		return false;
	};
	m_server->forEachEntity([&](civcraft::Entity& e) {
		if (!e.def().isItem()) return;
		if (e.removed) return;
		if (m_pickupRequests.count(e.id())) return;  // cooldown-gated
		if (hasAnim(e.id())) return;
		float dist = glm::length(e.position - pe->position);
		if (dist >= pdef.pickup_range) return;

		std::string itemType = e.getProp<std::string>(civcraft::Prop::ItemType);
		int count = e.getProp<int>(civcraft::Prop::Count, 1);
		const auto* bdef = m_server->blockRegistry().find(itemType);
		glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);

		if (pe->inventory && !pe->inventory->canAccept(itemType, count,
		                                                pdef.inventory_capacity)) {
			FloatText ft;
			ft.worldPos = pe->position + glm::vec3(0, 2.0f, 0);
			ft.color    = glm::vec3(0.95f, 0.55f, 0.35f);
			ft.text     = "Inventory full";
			ft.lifetime = 1.0f;
			m_floaters.push_back(ft);
			PickupRequest req;
			req.itemType    = itemType;
			req.count       = count;
			req.startPos    = e.position;
			req.color       = color;
			req.deniedShown = true;  // skip the denial path; cooldown still applies
			m_pickupRequests[e.id()] = req;
			return;
		}

		civcraft::ActionProposal p;
		p.type         = civcraft::ActionProposal::Relocate;
		p.actorId      = m_server->controlledEntityId();
		p.relocateFrom = civcraft::Container::entity(e.id());
		m_server->sendAction(p);

		PickupRequest req;
		req.itemType = itemType;
		req.count    = count;
		req.startPos = e.position;
		req.color    = color;
		m_pickupRequests[e.id()] = req;
	});
}


// Extracted from processInput — see game_vk.h.
void Game::drainDialogInput() {
	// Drain text input into DialogPanel if open. Consumed keys are removed
	// from the queue so nothing else sees them this frame.
	if (m_dialogPanel.isOpen()) {
		for (uint32_t cp : m_charQueue) m_dialogPanel.onChar(cp);
		m_charQueue.clear();
		std::vector<int> unconsumed;
		for (int k : m_keyQueue) {
			if (!m_dialogPanel.onKey(k)) unconsumed.push_back(k);
		}
		m_keyQueue.swap(unconsumed);
		// Push-to-talk: physical Y key state. Polled (not queued) so the
		// press/release edges land on the same frame the user produces them.
		bool yHeld = glfwGetKey(m_window, GLFW_KEY_Y) == GLFW_PRESS;
		m_dialogPanel.onPushToTalk(yHeld);
		// Feed streaming reply tokens into piper (sentence-by-sentence) so
		// the NPC voice overlaps the visible text animation.
		m_dialogPanel.tickVoice();
	} else {
		// No panel open — drop queued text so it doesn't fire a turn later.
		m_charQueue.clear();
		m_keyQueue.clear();
	}
}

void Game::processEscapeKey() {
	// ESC: cancel drag → close dialog → close inventory → dismiss panels → pause.
	// Each handler short-circuits so one tap doesn't cascade. Note: DialogPanel
	// already handles its own Esc via the key queue above, so this branch only
	// runs when the panel is closed or ignored the key.
	bool esc = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
	if (esc && !m_escLast) {
		if (m_rtsWheel.active) {
			m_rtsWheel = {};
		} else if (m_rtsDragCmd.active) {
			m_rtsDragCmd = {};
		} else if (m_drag.active) {
			m_drag = {};
		} else if (m_dialogPanel.isOpen()) {
			m_dialogPanel.close();
		} else if (m_invOpen) {
			m_invOpen = false;
		} else if (m_inspectedEntity != 0) {
			m_inspectedEntity = 0;
		} else if (m_handbookOpen) {
			m_handbookOpen = false;
		} else if (m_state == GameState::Playing) openGameMenu();
		else if (m_state == GameState::GameMenu) closeGameMenu();
	}
	m_escLast = esc;
}

void Game::processTalkKey() {
	// T: talk to humanoid NPC under cursor / crosshair.
	//   - Raycasts an entity (20b reach, same as RMB inspect)
	//   - Looks up its artifact; requires dialog_system_prompt to be set
	//   - Lazily spins up LlmClient + opens DialogPanel
	// Suppressed while the panel is already open (text input consumes 'T').
	bool tKey = glfwGetKey(m_window, GLFW_KEY_T) == GLFW_PRESS;
	if (tKey && !m_tKeyLast && !m_dialogPanel.isOpen() && m_server) {
		glm::vec3 eye = m_cam.position;
		glm::vec3 dir = m_cam.front();
		if (m_cam.mode == civcraft::CameraMode::RPG ||
		    m_cam.mode == civcraft::CameraMode::RTS) {
			double mx, my;
			glfwGetCursorPos(m_window, &mx, &my);
			if (m_fbW > 0 && m_fbH > 0) {
				float ndcX = (float)(mx / m_fbW) * 2.0f - 1.0f;
				float ndcY = 1.0f - (float)(my / m_fbH) * 2.0f;
				glm::mat4 invVP = glm::inverse(viewProj());
				glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				dir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
			}
		}
		std::vector<civcraft::RaycastEntity> ents;
		civcraft::EntityId myId = m_server->localPlayerId();
		m_server->forEachEntity([&](civcraft::Entity& e) {
			if (!e.def().isLiving()) return;
			if (!e.def().hasTag("humanoid")) return;
			ents.push_back({e.id(), e.typeId(), e.position,
				e.def().collision_box_min, e.def().collision_box_max,
				e.goalText, e.hasError});
		});
		auto hit = civcraft::raycastEntities(ents, eye, dir, 20.0f, myId);
		if (hit) {
			const auto* art = m_artifactRegistry.findById(hit->typeId);
			if (art) {
				if (!m_llmClient) {
					m_llmClient = std::make_unique<civcraft::llm::LlmClient>(
						"127.0.0.1", 8080);
				}
				std::string name = art->name.empty() ? hit->typeId : art->name;
				// Resolve this NPC's preferred voice (artifact field
				// `dialog_voice`; empty/missing → mux picks default).
				civcraft::llm::TtsClient* voice = nullptr;
				if (m_ttsMux) {
					std::string v;
					auto vIt = art->fields.find("dialog_voice");
					if (vIt != art->fields.end()) v = vIt->second;
					voice = m_ttsMux->clientFor(v);
				}
				if (!m_dialogPanel.open(hit->entityId, name, *art, *m_llmClient,
				                        m_audioCapture.get(), m_whisperClient.get(),
				                        voice, &m_audio)) {
					pushNotification(name + " has nothing to say.",
						glm::vec3(0.75f, 0.72f, 0.60f), 2.0f);
				}
			}
		}
	}
	m_tKeyLast = tKey;
}


// Extracted from tickPlayer — see game_vk.h.
void Game::processLmbInput(float dt) {
	(void)dt;  // per-frame state via members
	civcraft::Entity* me = playerEntity();
	if (!me) return;
	bool rtsMode = (m_cam.mode == civcraft::CameraMode::RTS);
	// LMB — FPS/TPS: swing (cone attack). RPG/RTS: click-move / box-select.
	int lmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT);
	bool lmbNow = (lmb == GLFW_PRESS);
	bool rtsLike = (m_cam.mode == civcraft::CameraMode::RPG || rtsMode);

	// RTS action wheel: modal. LMB picks a slice (or dismisses); cursor hover
	// highlights. Slice labels and their behavior:
	//   0 Gather / 1 Attack / 2 Mine — pause AI, walk to circle center, retain selection
	//   3 Cancel — resume AI, clear selection (same as legacy RMB-click)
	if (m_rtsWheel.active) {
		const float kRIn  = 0.04f;
		const float kROut = 0.14f;
		const float kPi   = 3.14159265f;
		double mxD, myD;
		glfwGetCursorPos(m_window, &mxD, &myD);
		float fbWf = (float)m_fbW, fbHf = (float)m_fbH;
		float ndcX = fbWf > 0 ? ((float)(mxD / fbWf) * 2.0f - 1.0f) : 0.0f;
		float ndcY = fbHf > 0 ? (1.0f - (float)(myD / fbHf) * 2.0f) : 0.0f;
		float aspect = (fbHf > 0 ? (fbWf / fbHf) : 1.0f);
		float dxn = ndcX - m_rtsWheel.centerNdc.x;
		float dyn = (ndcY - m_rtsWheel.centerNdc.y) / (aspect > 0 ? aspect : 1.0f);
		float dist = std::sqrt(dxn * dxn + dyn * dyn);
		int hover = -1;
		if (dist >= kRIn && dist <= kROut) {
			float ang = std::atan2(dyn, dxn);
			if (ang >= -kPi / 4 && ang <  kPi / 4)          hover = 1; // Attack (E)
			else if (ang >=  kPi / 4 && ang < 3 * kPi / 4)  hover = 0; // Gather (N)
			else if (ang >= -3 * kPi / 4 && ang < -kPi / 4) hover = 2; // Mine (S)
			else                                            hover = 3; // Cancel (W)
		}
		m_rtsWheel.hoverSlice = hover;
		m_rtsWheel.shiftQueue =
			(glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS) ||
			(glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

		if (lmbNow && !m_lmbLast) {
			int slice = hover;
			// Shift held → queue this command after the current plan instead of
			// replacing it. Cancel ignores shift (always clears everything).
			bool shiftQueue =
				(glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS) ||
				(glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
			if (slice == 3 || slice == -1) {
				// Cancel slice or click-outside-wheel: clear any client-side plan
				// and let the agent's default behavior resume.
				for (auto eid : m_rtsSelect.selected) {
					if (m_agentClient && eid != m_server->localPlayerId())
						m_agentClient->resumeAgent(eid);
					m_moveOrders.erase(eid);
				}
				std::printf("[vk-rts] wheel: %s %zu unit%s → default behavior\n",
					slice == 3 ? "CANCEL" : "dismissed",
					m_rtsSelect.selected.size(),
					m_rtsSelect.selected.size() == 1 ? "" : "s");
				m_rtsSelect.selected.clear();
			} else {
				// Gather/Attack/Mine: build a kind-specific Plan per agent and
				// install via pushPlanOverride (clears any prior plan, resets
				// the obey-pause timer). Selection is retained so the user can
				// re-command or switch to Cancel. Selected players have no
				// AgentClient — they fall back to the legacy walk-to-center.
				const char* label = (slice == 0 ? "GATHER"
				                   : slice == 1 ? "ATTACK" : "MINE");
				glm::vec3 ctr = m_rtsWheel.circleCenterWorld;
				float     rad = m_rtsWheel.circleRadiusWorld;

				// Attack: pick one shared target (nearest non-humanoid Living
				// inside the circle). Empty → fall back to walk-to-center,
				// so the slice still does *something* even on empty terrain.
				civcraft::EntityId attackTarget = civcraft::ENTITY_NONE;
				if (slice == 1) {
					float bestDist2 = rad * rad;
					m_server->forEachEntity([&](civcraft::Entity& e) {
						if (!e.def().isLiving()) return;
						if (e.def().hasTag("humanoid")) return;
						if (e.removed || e.hp() <= 0) return;
						glm::vec3 d = e.position - ctr; d.y = 0;
						float d2 = d.x * d.x + d.z * d.z;
						if (d2 <= bestDist2) {
							bestDist2 = d2;
							attackTarget = e.id();
						}
					});
				}

				glm::ivec3 goalBlock{(int)std::floor(ctr.x),
				                    (int)std::floor(ctr.y),
				                    (int)std::floor(ctr.z)};
				std::vector<civcraft::EntityId> playerEids;
				std::vector<glm::ivec3>         playerStarts;
				int handedToAgent = 0;

				for (auto eid : m_rtsSelect.selected) {
					civcraft::Entity* e = m_server->getEntity(eid);
					if (!e) continue;
					bool isPlayer = (eid == m_server->localPlayerId());
					if (isPlayer || !m_agentClient) {
						playerEids.push_back(eid);
						playerStarts.push_back(glm::ivec3(
							(int)std::floor(e->position.x),
							(int)std::floor(e->position.y),
							(int)std::floor(e->position.z)));
						continue;
					}

					civcraft::Plan plan;
					std::string    goalLabel;
					if (slice == 0) {
						auto step = civcraft::PlanStep::harvest(ctr);
						step.gatherTypes  = {"leaves", "logs"};
						step.gatherRadius = std::max(4.0f, std::min(rad, 12.0f));
						plan.push_back(step);
						goalLabel = "rts_gather";
					} else if (slice == 2) {
						auto step = civcraft::PlanStep::harvest(ctr);
						step.gatherTypes  = {"stone", "cobblestone",
						                    "granite", "marble", "sandstone"};
						step.gatherRadius = std::max(4.0f, std::min(rad, 12.0f));
						plan.push_back(step);
						goalLabel = "rts_mine";
					} else {
						if (attackTarget != civcraft::ENTITY_NONE) {
							plan.push_back(civcraft::PlanStep::move(ctr));
							plan.push_back(civcraft::PlanStep::attack(attackTarget));
							goalLabel = "rts_attack";
						} else {
							plan.push_back(civcraft::PlanStep::move(ctr));
							goalLabel = "rts_patrol";
						}
					}
					if (shiftQueue) {
						m_agentClient->appendPlanOverride(eid, std::move(plan),
						                                  std::move(goalLabel));
					} else {
						m_agentClient->pushPlanOverride(eid, std::move(plan),
						                                std::move(goalLabel));
					}
					++handedToAgent;
				}

				if (!playerEids.empty()) {
					m_rtsExec.planGroup(playerEids, playerStarts, goalBlock,
						m_server->chunks(), m_server->blockRegistry(),
						civcraft::CommandKind::Walk);
					for (auto eid : playerEids)
						m_moveOrders[eid] = {ctr, true};
				}

				std::printf("[vk-rts] wheel: %s%s %zu unit%s → (%.1f,%.1f,%.1f) "
				            "r=%.1f (agents=%d players=%zu target=%lld)\n",
					shiftQueue ? "+" : "",
					label, m_rtsSelect.selected.size(),
					m_rtsSelect.selected.size() == 1 ? "" : "s",
					ctr.x, ctr.y, ctr.z, rad,
					handedToAgent, playerEids.size(),
					(long long)attackTarget);
			}
			m_rtsWheel = {};
		}
		m_lmbLast = lmbNow;
		// Wheel is modal — skip all further LMB input processing this frame.
		return;
	}

	if (rtsMode && !m_uiWantsCursor) {
		double mx, my;
		glfwGetCursorPos(m_window, &mx, &my);
		int ww, wh;
		glfwGetWindowSize(m_window, &ww, &wh);
		float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
		float ndcY = 1.0f - (float)(my / wh) * 2.0f;

		if (lmbNow && !m_lmbLast) {
			m_rtsSelect.dragging = true;
			m_rtsSelect.start = {ndcX, ndcY};
			m_rtsSelect.end = {ndcX, ndcY};
			m_rtsLongPress.active    = true;
			m_rtsLongPress.startTime = glfwGetTime();
			m_rtsLongPress.startNdc  = {ndcX, ndcY};
		}
		if (m_rtsSelect.dragging && lmbNow) {
			m_rtsSelect.end = {ndcX, ndcY};
			if (m_rtsLongPress.active) {
				float dx = ndcX - m_rtsLongPress.startNdc.x;
				float dy = ndcY - m_rtsLongPress.startNdc.y;
				if (dx * dx + dy * dy > 0.0004f) m_rtsLongPress.active = false;
			}
		}

		if (m_rtsSelect.dragging && !lmbNow) {
			m_rtsSelect.dragging = false;
			float x0 = std::min(m_rtsSelect.start.x, m_rtsSelect.end.x);
			float x1 = std::max(m_rtsSelect.start.x, m_rtsSelect.end.x);
			float y0 = std::min(m_rtsSelect.start.y, m_rtsSelect.end.y);
			float y1 = std::max(m_rtsSelect.start.y, m_rtsSelect.end.y);
			bool isClick = (x1 - x0 < 0.02f && y1 - y0 < 0.02f);

			double holdSec = glfwGetTime() - m_rtsLongPress.startTime;
			bool isBuildCmd = m_rtsLongPress.active && isClick
			                  && holdSec >= kBuildHoldSec;
			m_rtsLongPress.active = false;

			if (isClick && !m_rtsSelect.selected.empty()) {
				glm::mat4 invVP = glm::inverse(pickViewProj());
				glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				glm::vec3 rayOrigin = glm::vec3(nearW);
				glm::vec3 dir = glm::normalize(glm::vec3(farW) - rayOrigin);
				auto hit = civcraft::raycastBlocks(m_server->chunks(), rayOrigin, dir, 200.0f);
				if (hit) {
					glm::vec3 center = glm::vec3(hit->blockPos) + glm::vec3(0.5f, 1.0f, 0.5f);
					civcraft::CommandKind kind = isBuildCmd
						? civcraft::CommandKind::Build : civcraft::CommandKind::Walk;
					std::vector<civcraft::EntityId> eids;
					std::vector<glm::ivec3> starts;
					for (auto eid : m_rtsSelect.selected) {
						civcraft::Entity* e = m_server->getEntity(eid);
						if (!e) continue;
						eids.push_back(eid);
						starts.push_back(glm::ivec3(
							(int)std::floor(e->position.x),
							(int)std::floor(e->position.y),
							(int)std::floor(e->position.z)));
					}
					if (!eids.empty()) {
						glm::ivec3 gi{(int)std::floor(center.x),
						              (int)std::floor(center.y),
						              (int)std::floor(center.z)};
						m_rtsExec.planGroup(eids, starts, gi,
							m_server->chunks(), m_server->blockRegistry(), kind);
						std::printf("[vk-rts] %s order: %zu entities → (%.1f,%.1f,%.1f) hold=%.2fs\n",
							isBuildCmd ? "BUILD" : "Move",
							eids.size(), center.x, center.y, center.z, holdSec);
						for (auto eid : eids) {
							m_moveOrders[eid] = {center, true};
							if (m_agentClient && eid != m_server->localPlayerId())
								m_agentClient->pauseAgent(eid);
						}
					}
				}
			} else if (!isClick) {
				// Shift-at-release: add to selection; no modifier: replace.
				bool shiftHeld =
					glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS ||
					glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
				std::unordered_set<civcraft::EntityId> existing;
				if (shiftHeld)
					existing.insert(m_rtsSelect.selected.begin(),
					                m_rtsSelect.selected.end());
				else
					m_rtsSelect.selected.clear();
				size_t added = 0;
				glm::mat4 vp = pickViewProj();
				m_server->forEachEntity([&](civcraft::Entity& e) {
					if (!e.def().isLiving()) return;
					if (!e.def().hasTag("humanoid")) return;
					float cy = e.position.y + e.def().collision_box_max.y * 0.5f;
					glm::vec3 anchor(e.position.x, cy, e.position.z);
					glm::vec4 clip = vp * glm::vec4(anchor, 1.0f);
					if (clip.w <= 0) return;
					float sx = clip.x / clip.w;
					float sy = clip.y / clip.w;
					if (sx < x0 || sx > x1 || sy < y0 || sy > y1) return;
					if (shiftHeld && existing.count(e.id())) return;
					m_rtsSelect.selected.push_back(e.id());
					added++;
				});
				if (added > 0)
					std::printf("[vk-rts] %s %zu unit%s (total %zu)\n",
						shiftHeld ? "added" : "selected", added,
						added == 1 ? "" : "s", m_rtsSelect.selected.size());
			}
		}

		// Drive commanded units, then clean up arrived/removed.
		m_rtsExec.driveRemote(*m_server, m_server->localPlayerId());
		std::vector<civcraft::EntityId> arrived;
		for (auto& [eid, order] : m_moveOrders) {
			if (!order.active || !m_server->getEntity(eid) || !m_rtsExec.has(eid))
				arrived.push_back(eid);
		}
		for (auto eid : arrived) {
			m_moveOrders.erase(eid);
			if (m_agentClient && eid != m_server->localPlayerId())
				m_agentClient->resumeAgent(eid);
		}
	} else if (!m_uiWantsCursor) {
		if (rtsLike) {
			// RPG/RTS: click-to-move on edge
			if (lmbNow && !m_lmbLast)
				clickToMove();
		} else {
			// FPS/TPS LMB: attack entity (if closer) or break block.
			bool lmbEdge = (lmbNow && !m_lmbLast);
			bool lmbHeld = (lmbNow && m_mouseCaptured);

			if (lmbEdge || lmbHeld) {
				glm::vec3 eye = m_cam.position;
				glm::vec3 dir = m_cam.front();

				// Entity attack priority. Held+CD so mash = hold (ARPG feel).
				bool entityAttacked = false;
				if ((lmbEdge || lmbHeld) && m_attackCD <= 0) {
					std::vector<civcraft::RaycastEntity> ents;
					civcraft::EntityId myId = m_server->localPlayerId();
					m_server->forEachEntity([&](civcraft::Entity& e) {
						if (!e.def().isLiving()) return;
						ents.push_back({e.id(), e.typeId(), e.position,
							e.def().collision_box_min, e.def().collision_box_max,
							e.goalText, e.hasError});
					});
					auto eHit = civcraft::raycastEntities(ents, eye, dir, 20.0f, myId);
					if (eHit) {
						auto blockHit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);
						bool entityCloser = !blockHit || eHit->distance <= blockHit->distance;
						if (entityCloser) {
							entityAttacked = true;
							m_attackCD = kTune.attackCD;
							auto* me = playerEntity();
							if (me) {
								Slash sw;
								sw.center = me->position + glm::vec3(0, kTune.playerHeight * 0.6f, 0);
								sw.dir    = playerForward();
								m_slashes.push_back(sw);
							}
							m_handSwingT = 0.0f;
							tryServerAttack();
						}
					}
				}

				// Block breaking (if no entity hit, break CD enforced).
				if (!entityAttacked && m_breakCD <= 0) {
					m_handSwingT = 0.0f;
					if (m_adminMode) {
						digInFront();
						m_breakCD = 0.15f;
					} else {
						auto hit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);
						if (hit) {
							glm::ivec3 bp = hit->blockPos;
							if (m_breaking.active && m_breaking.target == bp) {
								m_breaking.hits++;
							} else {
								m_breaking.target = bp;
								m_breaking.hits = 1;
								m_breaking.active = true;
							}
							m_breaking.timer = 0;
							m_breakCD = 0.25f;

							FloatText ft;
							ft.worldPos = glm::vec3(bp) + glm::vec3(0.5f, 1.2f, 0.5f);
							ft.color    = glm::vec3(0.9f, 0.8f, 0.6f);
							ft.text     = std::to_string(m_breaking.hits) + "/3";
							ft.lifetime = 0.5f;
							ft.rise     = 0.6f;
							m_floaters.push_back(ft);

							if (m_breaking.hits >= 3) {
								digInFront();
								m_breaking.active = false;
								m_breaking.hits = 0;
							}
						}
					}
				}
			}
		}
	}
	m_lmbLast = lmbNow;
}

void Game::processRmbInput(float dt) {
	(void)dt;
	// RMB — FPS/TPS: inspect (edge) or place (held+CD). RPG/RTS: click action.
	bool rmbPressed = false;
	bool rmbIsHeld  = false;
	{
		int rmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT);
		bool rmbNow = (rmb == GLFW_PRESS);
		bool rtsLikeMode = (m_cam.mode == civcraft::CameraMode::RPG ||
		                    m_cam.mode == civcraft::CameraMode::RTS);
		if (rtsLikeMode) {
			rmbPressed = m_rightClick.action;
			m_rightClick.action = false;
		} else {
			bool rmbEdge = (rmbNow && !m_rmbLast && m_mouseCaptured);
			rmbIsHeld    = (rmbNow && m_mouseCaptured);
			rmbPressed   = rmbEdge;
		}
		m_rmbLast = rmbNow;
	}

	// RTS drag-command state machine. In RTS mode with a non-empty selection,
	// RMB-hold-and-drag defines a ground circle; release opens the action wheel.
	// Pure RMB-click (no drag) falls through to the legacy release-selection
	// handler below, which cancels the selection and resumes AI — the same
	// semantics as the Cancel slice.
	if (m_cam.mode == civcraft::CameraMode::RTS
	    && !m_rtsSelect.selected.empty()
	    && !m_rtsWheel.active) {
		bool rmbDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT)
		               == GLFW_PRESS;
		double mxD, myD;
		glfwGetCursorPos(m_window, &mxD, &myD);
		float fbWf = (float)m_fbW, fbHf = (float)m_fbH;
		float ndcX = fbWf > 0 ? ((float)(mxD / fbWf) * 2.0f - 1.0f) : 0.0f;
		float ndcY = fbHf > 0 ? (1.0f - (float)(myD / fbHf) * 2.0f) : 0.0f;
		auto raycastGround = [&](float nx, float ny, glm::vec3& out) -> bool {
			glm::mat4 invVP = glm::inverse(pickViewProj());
			glm::vec4 nearW = invVP * glm::vec4(nx, ny, 0.0f, 1.0f); nearW /= nearW.w;
			glm::vec4 farW  = invVP * glm::vec4(nx, ny, 1.0f, 1.0f); farW  /= farW.w;
			glm::vec3 o = glm::vec3(nearW);
			glm::vec3 d = glm::normalize(glm::vec3(farW) - o);
			auto hit = civcraft::raycastBlocks(m_server->chunks(), o, d, 200.0f);
			if (!hit) return false;
			out = glm::vec3(hit->blockPos) + glm::vec3(0.5f, 1.0f, 0.5f);
			return true;
		};
		// Start drag on RMB press edge.
		if (rmbDown && !m_rtsDragCmd.active) {
			m_rtsDragCmd.active        = true;
			m_rtsDragCmd.startNdc      = {ndcX, ndcY};
			m_rtsDragCmd.currentNdc    = {ndcX, ndcY};
			m_rtsDragCmd.hasStartWorld = raycastGround(ndcX, ndcY,
			                                           m_rtsDragCmd.startWorld);
			m_rtsDragCmd.currentWorld  = m_rtsDragCmd.startWorld;
			m_rtsDragCmd.radiusWorld   = 0.0f;
		}
		// Track while held.
		if (rmbDown && m_rtsDragCmd.active) {
			m_rtsDragCmd.currentNdc = {ndcX, ndcY};
			if (m_rtsDragCmd.hasStartWorld) {
				glm::vec3 cur;
				if (raycastGround(ndcX, ndcY, cur)) {
					m_rtsDragCmd.currentWorld = cur;
					float dx = cur.x - m_rtsDragCmd.startWorld.x;
					float dz = cur.z - m_rtsDragCmd.startWorld.z;
					float r  = std::sqrt(dx * dx + dz * dz);
					// Hard cap — 16 blocks is already more than enough for any
					// wheel command. Lets the ring stop growing past the cap
					// while still tracking the cursor so the hover dot tracks.
					const float kMaxRadius = 16.0f;
					m_rtsDragCmd.radiusWorld = std::min(r, kMaxRadius);
				}
			}
		}
		// Release: open wheel on meaningful drag, else fall through as click.
		if (!rmbDown && m_rtsDragCmd.active) {
			float dxn = m_rtsDragCmd.currentNdc.x - m_rtsDragCmd.startNdc.x;
			float dyn = m_rtsDragCmd.currentNdc.y - m_rtsDragCmd.startNdc.y;
			bool dragged = (dxn * dxn + dyn * dyn) > 0.0004f
			               && m_rtsDragCmd.hasStartWorld
			               && m_rtsDragCmd.radiusWorld > 0.5f;
			if (dragged) {
				m_rtsWheel.active            = true;
				m_rtsWheel.centerNdc         = m_rtsDragCmd.currentNdc;
				m_rtsWheel.circleCenterWorld = m_rtsDragCmd.startWorld;
				m_rtsWheel.circleRadiusWorld = std::max(m_rtsDragCmd.radiusWorld, 1.5f);
				m_rtsWheel.hoverSlice        = -1;
				// Suppress the legacy click-release path — wheel controls the fate now.
				rmbPressed = false;
				m_rightClick.action = false;
			}
			m_rtsDragCmd.active = false;
		}
	}
	// RMB first takes precedence to release RTS-selected units back to their
	// default behavior (works in any camera mode — you can box-select in RTS,
	// switch to TPS, and still drop the group). Skips place/inspect when it fires.
	if (rmbPressed && !m_rtsSelect.selected.empty() && !m_rtsWheel.active) {
		for (auto eid : m_rtsSelect.selected) {
			if (m_agentClient && eid != m_server->localPlayerId())
				m_agentClient->resumeAgent(eid);
			m_moveOrders.erase(eid);
		}
		std::printf("[vk-rts] RMB released %zu unit%s → default behavior\n",
			m_rtsSelect.selected.size(),
			m_rtsSelect.selected.size() == 1 ? "" : "s");
		m_rtsSelect.selected.clear();
		rmbPressed = false;
		rmbIsHeld  = false;
	}
	// Held-place fires every kTune.placeCD s while RMB held in FPS/TPS.
	if (!rmbPressed && rmbIsHeld && m_placeCD <= 0) {
		rmbPressed = true;
	}
	if (rmbPressed && m_breakCD <= 0) {
		// Ray dir: cursor for RPG/RTS, camera forward otherwise.
		glm::vec3 eye = m_cam.position;
		glm::vec3 dir = m_cam.front();
		if (m_cam.mode == civcraft::CameraMode::RPG ||
		    m_cam.mode == civcraft::CameraMode::RTS) {
			double mx, my;
			glfwGetCursorPos(m_window, &mx, &my);
			int ww = m_fbW, wh = m_fbH;
			if (ww > 0 && wh > 0) {
				float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
				float ndcY = 1.0f - (float)(my / wh) * 2.0f;
				glm::mat4 invVP = glm::inverse(viewProj());
				glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				dir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
			}
		}

		// Entity inspect if closer than any block.
		bool inspectTriggered = false;
		{
			std::vector<civcraft::RaycastEntity> ents;
			civcraft::EntityId myId = m_server->localPlayerId();
			m_server->forEachEntity([&](civcraft::Entity& e) {
				if (!e.def().isLiving()) return;
				ents.push_back({e.id(), e.typeId(), e.position,
					e.def().collision_box_min, e.def().collision_box_max,
					e.goalText, e.hasError});
			});
			auto eHit = civcraft::raycastEntities(ents, eye, dir, 20.0f, myId);
			if (eHit) {
				auto blockHit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);
				bool entityCloser = !blockHit || eHit->distance < blockHit->distance;
				if (entityCloser) {
					m_inspectedEntity = eHit->entityId;
					m_server->sendGetInventory(eHit->entityId);
					inspectTriggered = true;
				}
			}
		}

		// Interact-first: doors, TNT, buttons. If the hit block isn't
		// interactive, fall through to placeBlock(). An open door grazed
		// along the ray counts as the interact target so you can click-close
		// a door you're standing in front of.
		if (!inspectTriggered) {
			bool didInteract = false;
			auto bHit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);
			if (bHit) {
				glm::ivec3 bp = bHit->hasInteract ? bHit->interactPos : bHit->blockPos;
				BlockId bid = bHit->hasInteract ? bHit->interactBlockId : bHit->blockId;
				const auto& bdef = m_server->blockRegistry().get(bid);
				bool interactive = (bdef.mesh_type == civcraft::MeshType::Door
				                 || bdef.mesh_type == civcraft::MeshType::DoorOpen
				                 || bdef.string_id == civcraft::BlockType::TNT);
				if (interactive) {
					civcraft::ActionProposal p;
					p.type     = civcraft::ActionProposal::Interact;
					p.actorId  = m_server->localPlayerId();
					p.blockPos = bp;
					m_server->sendAction(p);
					civcraft::GameLogger::instance().emit("ACTION",
						"interact @(%d,%d,%d)", bp.x, bp.y, bp.z);
					didInteract = true;
				}
			}
			if (!didInteract) {
				placeBlock();
				m_placeCD = kTune.placeCD;
			}
		}
	}

}

void Game::processMmbInput() {
	// MMB: rotate held block if it's rotatable, else eyedropper
	// (pick block type without breaking). Context-sensitive so we
	// keep both features without stealing a binding.
	int mmb = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_MIDDLE);
	bool mmbNow = (mmb == GLFW_PRESS);
	if (mmbNow && !m_mmbLast && !m_uiWantsCursor) {
		if (isHeldBlockRotatable()) {
			cyclePlacementRotation();
			m_mmbLast = mmbNow;
			return;
		}
		glm::vec3 eye = m_cam.position;
		glm::vec3 dir = m_cam.front();
		auto hit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 16.0f);
		if (hit) {
			const auto& bdef = m_server->blockRegistry().get(hit->blockId);
			// MMB eyedropper: select the hotbar slot that holds this block
			// type (if any) so RMB will place it. Slots not carrying the
			// item remain untouched; if no slot has it, selection stays put.
			auto* me = playerEntity();
			if (me && me->inventory) {
				for (int s = 0; s < civcraft::Hotbar::SLOTS; s++) {
					if (m_hotbar.get(s) == bdef.string_id &&
					    me->inventory->count(bdef.string_id) > 0) {
						m_hotbar.selected = s;
						break;
					}
				}
			}
			FloatText ft;
			ft.worldPos = glm::vec3(hit->blockPos) + glm::vec3(0.5f, 1.4f, 0.5f);
			ft.color    = glm::vec3(0.55f, 0.75f, 1.0f);
			std::string name = bdef.string_id;
			auto colon = name.find(':');
			if (colon != std::string::npos) name = name.substr(colon + 1);
			ft.text     = "PICK: " + name;
			ft.lifetime = 0.6f;
			m_floaters.push_back(ft);
		}
	}
	m_mmbLast = mmbNow;
}

} // namespace civcraft::vk

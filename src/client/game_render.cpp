#include "client/game.h"
#include "imgui.h"
#include <unordered_set>
#include <filesystem>

namespace modcraft {

// ============================================================
// renderPlaying helpers
// ============================================================

void Game::renderWorld(float dt, float aspect) {
	auto& srv = *m_server;

	// Update chunks
	m_renderer.updateChunks(srv.chunks(), m_camera, m_renderDistance);

	// Render terrain + sky + crosshair
	auto& hit = m_gameplay.currentHit();
	glm::ivec3 hlPos;
	glm::ivec3* hlPtr = nullptr;
	if (hit) {
		hlPos = hit->blockPos;
		hlPtr = &hlPos;
	}
	int selectedSlot = playerEntity()->getProp<int>(Prop::SelectedSlot, 0);

	// Crosshair position depends on camera mode:
	// - FPS: center of screen
	// - ThirdPerson/RPG: no crosshair (targeted block shown via highlight wireframe)
	// - RTS: no crosshair (mouse cursor visible)
	glm::vec2 crosshairOffset = {0, 0};
	bool showCrosshair = (m_camera.mode == CameraMode::FirstPerson);

	m_renderer.render(m_camera, aspect, hlPtr, selectedSlot, 7, crosshairOffset, showCrosshair);

	// Fog of war — render fog at unloaded chunk boundaries
	m_renderer.renderFogOfWar(m_camera, aspect, srv.chunks(), m_renderDistance);

	// Move target highlight (RPG/RTS click-to-move destination)
	if (m_gameplay.hasMoveTarget()) {
		glm::ivec3 targetBlock = glm::ivec3(glm::floor(m_gameplay.moveTarget() - glm::vec3(0, 1, 0)));
		m_renderer.renderMoveTarget(m_camera, aspect, targetBlock);
	}

	// Block break progress overlay (survival multi-hit)
	if (m_gameplay.isBreaking()) {
		m_renderer.renderBreakProgress(m_camera, aspect,
			m_gameplay.breakTarget(), m_gameplay.breakProgress());
	}

	// Door swing animation overlay
	m_renderer.renderDoorAnims(m_camera, aspect, m_doorAnims);
}

void Game::renderEntities(float dt, float aspect) {
	Entity* pe = playerEntity();
	auto& srv = *m_server;
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	// Player walk animation (from entity velocity + walk distance)
	m_globalTime += dt;
	float playerSpeed = glm::length(glm::vec2(pe->velocity.x, pe->velocity.z));
	float prevWalkDist = m_playerWalkDist;
	m_playerWalkDist += playerSpeed * dt;
	float armPitch = 0.f, armYaw = 0.f;
	m_attackAnim.currentArmAngles(armPitch, armYaw);

	// Clip toggles: C = dance. Pressing again cancels. Walking cancels too
	// so the clip doesn't override the walk cycle while moving.
	if (m_controls.pressed(Action::Dance)) {
		m_playerClip = (m_playerClip == "dance") ? "" : "dance";
	}
	if (playerSpeed > 0.5f) m_playerClip.clear();

	AnimState playerAnim = {};
	playerAnim.walkDistance = m_playerWalkDist;
	playerAnim.speed        = playerSpeed;
	playerAnim.time         = m_globalTime;
	playerAnim.attackPhase  = m_attackAnim.phase();
	playerAnim.armPitch     = armPitch;
	playerAnim.armYaw       = armYaw;
	playerAnim.currentClip  = m_playerClip;
	// Head/body look tracking.
	// Priority: action target (attack/mine) > camera look direction.
	// The head rotates up to ±45° from the body; excess yaw rotates the body.
	float headYawDeg = 0.0f;
	float targetPitchDeg = 0.0f;
	float bodyYawOffset = 0.0f; // extra body rotation applied at draw time
	{
		constexpr float kHeadYawMax = 45.0f;
		float bodyYaw = m_camera.player.yaw;  // current body facing (degrees)
		bool hasActionTarget = false;
		// Head tracking only in TPS (camera is behind player, head follows look).
		// In RPG/RTS the camera is detached — head faces forward.
		// During named clips (dance, wave, etc.) head stays neutral.
		bool trackCamera = (m_camera.mode == CameraMode::ThirdPerson)
		                    && m_playerClip.empty();
		float targetWorldYaw = trackCamera ? m_camera.lookYaw : bodyYaw;
		if (trackCamera) targetPitchDeg = m_camera.lookPitch;

		// Action target: look at attacked entity or mined block
		if (m_attackAnim.active() || m_gameplay.isBreaking()) {
			glm::vec3 targetPos;
			bool gotTarget = false;

			if (m_lastAttackTargetId != ENTITY_NONE) {
				Entity* target = m_server->getEntity(m_lastAttackTargetId);
				if (target) {
					targetPos = target->position + (target->eyePos() - target->position) * 0.5f;
					gotTarget = true;
				}
			}
			if (!gotTarget && m_gameplay.isBreaking()) {
				targetPos = glm::vec3(m_gameplay.breakTarget()) + glm::vec3(0.5f);
				gotTarget = true;
			}

			if (gotTarget) {
				glm::vec3 delta = targetPos - pe->position;
				targetWorldYaw = glm::degrees(atan2(delta.z, delta.x));
				float hDist = glm::length(glm::vec2(delta.x, delta.z));
				float eyeH = pe->eyePos().y - pe->position.y;
				targetPitchDeg = glm::degrees(atan2(delta.y - eyeH, hDist));
				hasActionTarget = true;
			}
		}

		// Compute relative yaw from the *effective* body direction (body + current
		// offset) so the offset accumulates continuously — no 180° snap.
		float& smoothed = hasActionTarget ? m_actionBodyYawOffset : m_cameraBodyYawOffset;
		float effectiveBodyYaw = bodyYaw + smoothed;
		float relYaw = targetWorldYaw - effectiveBodyYaw;
		while (relYaw >  180.f) relYaw -= 360.f;
		while (relYaw < -180.f) relYaw += 360.f;

		// Head covers ±45°; excess drives continuous body rotation
		headYawDeg = glm::clamp(relYaw, -kHeadYawMax, kHeadYawMax);
		float excess = relYaw - headYawDeg;

		float lerpRate = hasActionTarget ? 15.0f : 10.0f;
		smoothed += excess * std::min(lerpRate * dt, 1.0f);

		// Decay whichever offset is NOT active toward 0
		float& inactive = hasActionTarget ? m_cameraBodyYawOffset : m_actionBodyYawOffset;
		inactive *= std::max(0.0f, 1.0f - dt * 8.0f);

		// When walking, decay camera body offset so body faces movement direction.
		// The faster the player moves, the stronger the pull back to forward.
		if (!hasActionTarget) {
			float walkDecay = std::min(playerSpeed * 3.0f * dt, 1.0f);
			m_cameraBodyYawOffset *= (1.0f - walkDecay);
		}

		// In RPG/RTS the camera is detached — decay all offsets to zero.
		if (!trackCamera && !hasActionTarget) {
			float decay = std::min(dt * 8.0f, 1.0f);
			m_cameraBodyYawOffset *= (1.0f - decay);
			m_actionBodyYawOffset *= (1.0f - decay);
		}

		bodyYawOffset = m_actionBodyYawOffset + m_cameraBodyYawOffset;
	}
	// Negate head yaw: model root uses -yaw, so model-local Y rotation is mirrored
	playerAnim.lookYaw   = glm::radians(-headYawDeg);
	playerAnim.lookPitch = glm::radians(glm::clamp(targetPitchDeg, -45.f, 45.f));

	// Footstep sounds — play every ~2.5 blocks of movement
	if (pe->onGround && playerSpeed > 0.5f) {
		float stepInterval = 2.5f;
		if ((int)(m_playerWalkDist / stepInterval) != (int)(prevWalkDist / stepInterval)) {
			glm::ivec3 feetBlock = glm::ivec3(glm::floor(pe->position)) - glm::ivec3(0, 1, 0);
			BlockId underFeet = srv.chunks().getBlock(feetBlock.x, feetBlock.y, feetBlock.z);
			const auto& bdef = srv.blockRegistry().get(underFeet);
			std::string stepSound = bdef.sound_footstep;
			if (stepSound.empty()) stepSound = "step_dirt";
			m_audio.play(stepSound, pe->position, 0.35f);
		}
	}

	// Resolve model key: character_skin prop overrides EntityDef.model
	auto resolveModelKey = [](const Entity& e) -> std::string {
		std::string skin = e.getProp<std::string>("character_skin", "");
		if (!skin.empty()) {
			auto colon = skin.find(':');
			return (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
		}
		std::string key = e.def().model;
		auto dot = key.rfind('.');
		if (dot != std::string::npos) key = key.substr(0, dot);
		return key;
	};

	// Draw local player — skip in first-person (camera at eyes)
	if (m_camera.mode != CameraMode::FirstPerson) {
		auto pit = m_models.find(resolveModelKey(*pe));
		if (pit != m_models.end()) {
			// Resolve held items: hotbar selected → main hand,
			// offhand inventory slot → opposite (or chosen) hand.
			HeldItems held;
			auto resolveItemModel = [&](const std::string& itemId) -> const BoxModel* {
				if (itemId.empty()) return nullptr;
				std::string key = itemId;
				auto colon = key.find(':');
				if (colon != std::string::npos) key = key.substr(colon + 1);
				auto mit = m_models.find(key);
				return (mit != m_models.end()) ? &mit->second : nullptr;
			};

			int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
			std::string mainItemId = m_hotbar.get(slot);
			if (!mainItemId.empty() && pe->inventory
			    && m_hotbar.count(slot, *pe->inventory) <= 0) {
				mainItemId.clear();
			}
			std::string offhandItemId =
				pe->inventory ? pe->inventory->equipped(WearSlot::Offhand) : "";
			bool offhandRight = pe->inventory && pe->inventory->offhandInRightHand();

			HeldItem mainItem;  mainItem.model = resolveItemModel(mainItemId);
			HeldItem offItem;   offItem.model  = resolveItemModel(offhandItemId);
			if (offhandRight) {
				held.rightHand = offItem;
				held.leftHand  = mainItem;
			} else {
				held.rightHand = mainItem;
				held.leftHand  = offItem;
			}

			mr.draw(pit->second, vp, m_camera.smoothedFeetPos(),
			        m_camera.player.yaw + bodyYawOffset, playerAnim, 0.0f,
			        glm::vec3(1.0f, 0.15f, 0.15f),
			        glm::normalize(glm::vec3(0.5f, 0.85f, 0.3f)),
			        &held);
		}
	}

	// Mob models — all entities except the locally-possessed one (drawn above)
	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return;

		float mobSpeed = e.getProp<float>(Prop::AnimSpeed, 0.0f);
		float mobDist = e.getProp<float>(Prop::WalkDistance, 0.0f);
		AnimState mobAnim = {};
		mobAnim.walkDistance = mobDist;
		mobAnim.speed        = mobSpeed;
		mobAnim.time         = m_globalTime;

		std::string modelKey = resolveModelKey(e);
		auto mit = m_models.find(modelKey);
		if (mit != m_models.end()) {
			const BoxModel& mobModel = mit->second;

			// Damage flash: entity flashes red for a short time after being hit
			float flashT = 0.0f;
			auto flit = m_damageFlash.find(e.id());
			if (flit != m_damageFlash.end()) flashT = flit->second;
			float tintStr = std::max(0.0f, flashT / 0.25f);

			// Client-side animation clip selection for mobs.
			// Driven purely from observable entity state (goal text) — server
			// has no knowledge of animations per Rule 0. Keyword table is
			// additive: modders can extend behaviors and add matching clip
			// names to their model's `clips` dict.
			auto pickClip = [](const std::string& goal) -> const char* {
				if (goal.empty()) return "";
				if (goal.find("Chopping")    != std::string::npos) return "chop";
				if (goal.find("Mining")      != std::string::npos) return "mine";
				if (goal.find("Sleeping")    != std::string::npos) return "sleep";
				if (goal.find("Depositing")  != std::string::npos) return "wave";
				if (goal.find("Dancing")     != std::string::npos) return "dance";
				return "";
			};
			mobAnim.currentClip = pickClip(e.goalText);

			// Remote head tracking: split lookYaw into head + body overflow.
			// AI creatures get lookYaw=bodyYaw from the server, so their
			// heads naturally face forward with no special-casing here.
			float remoteBodyYaw = e.yaw;
			if (e.def().isLiving()) {
				constexpr float kMax = 45.0f;
				float rel = e.lookYaw - e.yaw;
				while (rel >  180.f) rel -= 360.f;
				while (rel < -180.f) rel += 360.f;
				float headDeg = glm::clamp(rel, -kMax, kMax);
				remoteBodyYaw = e.yaw + (rel - headDeg);
				mobAnim.lookYaw   = glm::radians(-headDeg);
				mobAnim.lookPitch = glm::radians(glm::clamp(e.lookPitch, -45.f, 45.f));
			}
			mr.draw(mobModel, vp, e.position, remoteBodyYaw, mobAnim, tintStr);
		} else if (!modelKey.empty() && e.typeId() != ItemName::ItemEntity) {
			// Warn once per model key
			static std::unordered_set<std::string> warned;
			if (warned.insert(modelKey).second)
				printf("[Render] WARNING: no model for key '%s' (entity %s)\n",
					modelKey.c_str(), e.typeId().c_str());
		} else if (e.typeId() == ItemName::ItemEntity) {
			// If pickup is pending, don't render the server entity at all.
			if (m_pendingPickups.count(e.id())) return;

			// Floating in place — bob + bounce + spin + XZ scatter (client-side)
			unsigned int h = e.id() * 2654435761u;
			float bob = std::sin(m_globalTime * 2.5f + e.id() * 1.7f) * 0.06f;
			float bounce = std::abs(std::sin(m_globalTime * 4.0f + e.id() * 2.3f)) * 0.04f;
			float bobY = bob + bounce;
			float spinYaw = m_globalTime * 90.0f + e.id() * 47.0f;
			// Scatter: small XZ offset so stacked items don't overlap
			float ox = ((h & 0xFF) / 255.0f - 0.5f) * 0.3f;
			float oz = (((h >> 8) & 0xFF) / 255.0f - 0.5f) * 0.3f;
			std::string itemType = e.getProp<std::string>(Prop::ItemType);
			std::string itemModelKey = itemType;
			auto colon = itemModelKey.find(':');
			if (colon != std::string::npos) itemModelKey = itemModelKey.substr(colon + 1);
			BoxModel itemModel;
			auto imIt = m_models.find(itemModelKey);
			if (imIt != m_models.end()) {
				// Use the real model, height-normalized so all items are ~0.35 blocks tall
				itemModel = imIt->second;
				float targetH = 0.35f;
				float modelH = std::max(itemModel.totalHeight * itemModel.modelScale, 0.1f);
				float worldScale = targetH / modelH;
				for (auto& part : itemModel.parts) {
					part.offset *= worldScale;
					part.halfSize *= worldScale;
				}
			} else {
				// Fallback: colored cube
				const BlockDef* idef = srv.blockRegistry().find(itemType);
				glm::vec3 itemColor = idef ? idef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
				BodyPart bp;
				bp.offset = {0, 0.15f, 0};
				bp.halfSize = {0.12f, 0.12f, 0.12f};
				bp.color = {itemColor.r, itemColor.g, itemColor.b, 1.0f};
				itemModel.parts.push_back(bp);
			}
			mr.draw(itemModel, vp, e.position + glm::vec3(ox, bobY + 0.3f, oz), spinYaw, {});
		}
	});

	renderPickupAnimations();

	// Selection circles under selected entities (RTS mode)
	{
		auto& sel = m_gameplay.selectedEntities();
		for (EntityId eid : sel) {
			Entity* se = srv.getEntity(eid);
			if (!se) continue;
			glm::ivec3 selBlock = glm::ivec3(glm::floor(se->position));
			m_renderer.renderMoveTarget(m_camera, aspect, selBlock);
		}
	}

	renderEntityEffects(dt, aspect);

	// Particles
	m_particles.render(vp);
}

void Game::renderPickupAnimations() {
	Entity* pe = playerEntity();
	float aspect = (float)m_window.width() / (float)m_window.height();
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	for (auto& pa : m_pickupAnims) {
		float ease = pa.t * pa.t * (3.0f - 2.0f * pa.t);
		glm::vec3 target = pe->position + glm::vec3(0, 0.8f, 0);
		glm::vec3 drawPos = glm::mix(pa.startPos, target, ease);
		float scale = 1.0f - ease * 0.5f;
		float spinYaw = m_globalTime * 720.0f;
		BoxModel flyModel;
		auto fmit = m_models.find(pa.modelKey);
		if (fmit != m_models.end()) {
			flyModel = fmit->second;
			float modelH = std::max(flyModel.totalHeight * flyModel.modelScale, 0.1f);
			float flyScale = (0.35f / modelH) * scale;
			for (auto& part : flyModel.parts) {
				part.offset *= flyScale;
				part.halfSize *= flyScale;
			}
		} else {
			float hs = 0.12f * scale;
			BodyPart bp;
			bp.offset = {0, 0.15f, 0};
			bp.halfSize = {hs, hs, hs};
			bp.color = {pa.color.r, pa.color.g, pa.color.b, 1.0f};
			flyModel.parts.push_back(bp);
		}
		mr.draw(flyModel, vp, drawPos, spinYaw, {});
	}
}

void Game::renderEntityEffects(float dt, float aspect) {
	Entity* pe = playerEntity();
	auto& srv = *m_server;
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	// Resolve model key helper (same logic as renderEntities)
	auto resolveModelKey = [](const Entity& e) -> std::string {
		std::string skin = e.getProp<std::string>("character_skin", "");
		if (!skin.empty()) {
			auto colon = skin.find(':');
			return (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
		}
		std::string key = e.def().model;
		auto dot = key.rfind('.');
		if (dot != std::string::npos) key = key.substr(0, dot);
		return key;
	};

	// Lightbulb icon (UI indicator above AI entities, not game content)
	static BoxModel lightbulb = []() {
		BoxModel m; m.totalHeight = 0.4f;
		auto mk = [](glm::vec3 off, glm::vec3 half, glm::vec4 col) {
			BodyPart p; p.offset = off; p.halfSize = half; p.color = col; return p;
		};
		m.parts.push_back(mk({0,0.15f,0},{0.08f,0.10f,0.08f},{1.0f,0.92f,0.3f,0.9f}));
		m.parts.push_back(mk({0,0.27f,0},{0.05f,0.04f,0.05f},{1.0f,1.0f,0.7f,0.95f}));
		m.parts.push_back(mk({0,0.04f,0},{0.06f,0.05f,0.06f},{0.5f,0.5f,0.5f,0.9f}));
		return m;
	}();

	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return;
		if (!e.def().isLiving()) return; // only living entities

		float entityTop = e.def().collision_box_max.y;
		float bobY = std::sin(m_globalTime * 2.0f + e.id() * 0.7f) * 0.05f;
		glm::vec3 bulbPos = e.position + glm::vec3(0, entityTop + 0.3f + bobY, 0);

		if (m_showGoalBubbles) {
			// Red tint if behavior has error
			BoxModel bulb = lightbulb;
			if (e.hasError) {
				for (auto& p : bulb.parts)
					p.color = {1.0f, 0.2f, 0.2f, 0.9f};
			}
			mr.draw(bulb, vp, bulbPos, m_camera.lookYaw, {}); // billboard: face camera
		}

		// Goal change detection — log to in-game overlay and trigger pop animation
		{
			auto& prev = m_entityGoals[e.id()];
			if (!e.goalText.empty() && e.goalText != prev) {
				prev = e.goalText;
				std::string typeName = e.typeId();
				auto col = typeName.find(':');
				if (col != std::string::npos) typeName = typeName.substr(col + 1);
				if (!typeName.empty()) typeName[0] = (char)toupper((unsigned char)typeName[0]);
				appendLog(typeName + " #" + std::to_string(e.id()) + ": " + e.goalText);
				m_entityGoalPopTimer[e.id()] = 3.0f;  // text visible for 3s then fades
			}
		}

		// HP tracking: detect damage/death for flash, log, sound, puff
		{
			int curHP = e.getProp<int>(Prop::HP, e.def().max_hp);
			auto hpIt = m_prevEntityHP.find(e.id());
			if (hpIt != m_prevEntityHP.end() && curHP < hpIt->second) {
				int dmg = hpIt->second - curHP;
				bool dying = (curHP <= 0);
				std::string eName = e.def().display_name.empty() ? e.typeId() : e.def().display_name;
				appendLog(dying ? eName + " died" : eName + " took " + std::to_string(dmg) + " damage");

				// Red flash: set/reset timer on any damage
				m_damageFlash[e.id()] = 0.25f;

				// Floating damage number
				{
					bool isPlayer = (e.id() == m_server->localPlayerId());
					FloatTextEvent ft;
					ft.source   = isPlayer ? FloatSource::DamageTaken : FloatSource::DamageDealt;
					ft.targetId = e.id();
					ft.worldPos = e.position + glm::vec3(0.0f, entityTop * 0.5f, 0.0f);
					ft.value    = (float)dmg;
					ft.isSplash = true;
					ft.isDying  = dying;
					m_floatText.add(ft);
				}

				// Hitmarker crosshair feedback
				m_renderer.triggerHitmarker(dying);

				// Impact sound
				if (dying) {
					m_audio.play("hit_punch", e.position, 1.0f);
					// Death puff: particle burst at entity center using its body color
					glm::vec3 bodyColor = {0.7f, 0.55f, 0.35f};
					auto fmit = m_models.find(resolveModelKey(e));
					if (fmit != m_models.end() && !fmit->second.parts.empty())
						bodyColor = glm::vec3(fmit->second.parts[0].color);
					m_particles.emitDeathPuff(e.position, bodyColor, entityTop);
				} else {
					m_audio.play(dmg >= 4 ? "hit_sword" : "hit_punch", e.position, 0.6f);
				}
			}
			m_prevEntityHP[e.id()] = curHP;
		}
		// Decay timers
		{
			auto flit = m_damageFlash.find(e.id());
			if (flit != m_damageFlash.end()) {
				flit->second -= dt;
				if (flit->second <= 0) m_damageFlash.erase(flit);
			}
			auto plit = m_entityGoalPopTimer.find(e.id());
			if (plit != m_entityGoalPopTimer.end()) {
				plit->second -= dt;
				if (plit->second <= 0) m_entityGoalPopTimer.erase(plit);
			}
		}
	});

	// Remove HP / flash entries for entities no longer in the world
	{
		std::unordered_set<EntityId> seen;
		m_server->forEachEntity([&](Entity& e) { if (e.def().isLiving()) seen.insert(e.id()); });
		for (auto it = m_prevEntityHP.begin(); it != m_prevEntityHP.end(); ) {
			if (seen.count(it->first)) { ++it; continue; }
			m_floatText.onEntityRemoved(it->first);
			it = m_prevEntityHP.erase(it);
		}
		for (auto it = m_damageFlash.begin(); it != m_damageFlash.end(); )
			it = seen.count(it->first) ? std::next(it) : m_damageFlash.erase(it);
		for (auto it = m_entityGoalPopTimer.begin(); it != m_entityGoalPopTimer.end(); )
			it = seen.count(it->first) ? std::next(it) : m_entityGoalPopTimer.erase(it);
		for (auto it = m_entityGoals.begin(); it != m_entityGoals.end(); )
			it = seen.count(it->first) ? std::next(it) : m_entityGoals.erase(it);
	}
}

void Game::renderHUD(float dt, float aspect, bool skipImGui) {
	Entity* pe = playerEntity();
	auto& srv = *m_server;
	auto& mr = m_renderer.modelRenderer();
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();

	// ── First-person held item (Minecraft-style, bottom-right) ──
	if (m_camera.mode == CameraMode::FirstPerson && pe->inventory) {
		int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
		std::string heldId = m_hotbar.get(slot);
		if (!heldId.empty() && m_hotbar.count(slot, *pe->inventory) > 0) {
			std::string fpKey = heldId;
			auto fpColon = fpKey.find(':');
			if (fpColon != std::string::npos) fpKey = fpKey.substr(fpColon + 1);
			auto fpMit = m_models.find(fpKey);
			if (fpMit != m_models.end()) {
				// Clear depth so held item renders on top of world
				glClear(GL_DEPTH_BUFFER_BIT);

				glm::mat4 fpProj = glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
				glm::mat4 fpView = glm::lookAt(
					glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
				glm::mat4 fpVP = fpProj * fpView;

				// Position: bottom-right of view (Minecraft-style)
				float bobAmt = pe->onGround
					? glm::length(glm::vec2(pe->velocity.x, pe->velocity.z)) : 0.0f;
				float bobScale = std::min(bobAmt / 4.0f, 1.0f);
				float bobY = std::sin(m_playerWalkDist * 6.0f) * 0.018f * bobScale;
				float bobX = std::sin(m_playerWalkDist * 3.0f) * 0.010f * bobScale;
				glm::vec3 itemPos(0.55f + bobX, -0.38f + bobY, -0.70f);

				glm::mat4 fpRoot = glm::translate(glm::mat4(1.0f), itemPos);

				// Apply equip rotation from model
				auto& eqt = fpMit->second.equip;
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.y), glm::vec3(0, 1, 0));
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.x), glm::vec3(1, 0, 0));
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.z), glm::vec3(0, 0, 1));

				// Attack animation: keyframe-driven position + rotation delta.
				if (m_attackAnim.active()) {
					glm::vec3 dPos, dRot;
					m_attackAnim.currentDelta(dPos, dRot);
					fpRoot = glm::translate(fpRoot, dPos);
					fpRoot = glm::rotate(fpRoot, glm::radians(dRot.x), glm::vec3(1, 0, 0));
					fpRoot = glm::rotate(fpRoot, glm::radians(dRot.y), glm::vec3(0, 1, 0));
					fpRoot = glm::rotate(fpRoot, glm::radians(dRot.z), glm::vec3(0, 0, 1));
				}

				// Auto-scale items without explicit equip transform
				float fpEs = eqt.scale;
				bool fpHasEquip = (eqt.rotation != glm::vec3(0) || eqt.offset != glm::vec3(0) || eqt.scale != 1.0f);
				if (!fpHasEquip) {
					float mh = std::max(fpMit->second.totalHeight * fpMit->second.modelScale, 0.1f);
					fpEs = std::min(0.35f / mh, 0.5f);
				}
				float fpScale = fpEs * 0.72f;
				fpRoot = glm::scale(fpRoot, glm::vec3(fpScale));

				mr.drawStatic(fpMit->second, fpVP, fpRoot);
			}
		}
	}

	// HUD — read all player state from entity
	int selectedSlot = pe->getProp<int>(Prop::SelectedSlot, 0);
	auto& hit = m_gameplay.currentHit();
	int playerHP = pe->getProp<int>(Prop::HP, pe->def().max_hp);
	float playerHunger = pe->getProp<float>(Prop::Hunger, 20.0f);
	Inventory emptyInv;
	HUDContext ctx{
		aspect, m_state, selectedSlot,
		pe->inventory ? *pe->inventory : emptyInv,
		m_hotbar,
		m_camera, srv.blockRegistry(), &srv.chunks(),
		m_worldTime, m_currentFPS, m_showDebug, m_equipUI.isOpen(),
		hit, m_gameplay.currentEntityHit(),
		m_renderer.sunStrength(),
		srv.entityCount(), m_particles.count(),
		playerHP, pe->def().max_hp, playerHunger
	};
	m_hud.render(ctx, m_text, m_renderer.highlightShader());

	// ImGui overlays — skip when another ImGui overlay is active
	if (skipImGui) return;
	m_ui.beginFrame();
	m_iconCache.setTime(m_globalTime);

	// ── ImGui Hotbar (Roboto font + rotating 3D block preview) ──
	{
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		float ww = (float)m_window.width(), wh = (float)m_window.height();
		int slots = Hotbar::SLOTS;
		float slotPx = 60.0f;
		float gapPx = 4.0f;
		float totalW = slots * (slotPx + gapPx) - gapPx;
		float startX = (ww - totalW) * 0.5f;
		float startY = wh - slotPx - 12.0f;

		// Backdrop panel
		float pad = 8.0f;
		dl->AddRectFilled(
			{startX - pad, startY - pad},
			{startX + totalW + pad, startY + slotPx + pad},
			IM_COL32(16, 14, 10, 185), 8.0f);
		dl->AddRect(
			{startX - pad, startY - pad},
			{startX + totalW + pad, startY + slotPx + pad},
			IM_COL32(90, 72, 45, 170), 8.0f, 0, 1.5f);

		Inventory emptyInv2;
		const Inventory& inv = pe->inventory ? *pe->inventory : emptyInv2;
		auto& blocks = srv.blockRegistry();

		for (int i = 0; i < slots; i++) {
			float sx = startX + i * (slotPx + gapPx);
			float sy = startY;
			bool selected = (i == selectedSlot);

			// Slot bg
			ImU32 slotBg = selected ? IM_COL32(72, 58, 30, 230) : IM_COL32(28, 24, 18, 210);
			dl->AddRectFilled({sx, sy}, {sx + slotPx, sy + slotPx}, slotBg, 4.0f);

			// Selection glow
			if (selected) {
				dl->AddRect({sx - 2, sy - 2}, {sx + slotPx + 2, sy + slotPx + 2},
					IM_COL32(225, 175, 50, 230), 5.0f, 0, 2.5f);
			} else {
				dl->AddRect({sx, sy}, {sx + slotPx, sy + slotPx},
					IM_COL32(65, 52, 36, 140), 4.0f, 0, 1.0f);
			}

			// Item content: 3D model icon
			std::string itemId = m_hotbar.get(i);
			int itemCount = m_hotbar.count(i, inv);
			if (!itemId.empty() && itemCount > 0) {
				std::string modelKey = itemId;
				auto colon = modelKey.find(':');
				if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);

				auto mit = m_models.find(modelKey);
				GLuint icon = (mit != m_models.end())
					? m_iconCache.getIcon(modelKey, mit->second) : 0;

				float ipad = 4.0f;
				if (icon) {
					dl->AddImage((ImTextureID)(intptr_t)icon,
						{sx + ipad, sy + ipad}, {sx + slotPx - ipad, sy + slotPx - ipad},
						{0, 1}, {1, 0});
				} else {
					// Fallback: colored cube for blocks without a model file
					const BlockDef* bdef = blocks.find(itemId);
					glm::vec3 c = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.75f);
					float cx = sx + slotPx * 0.5f;
					float cy = sy + slotPx * 0.40f;
					float sz = slotPx * 0.34f;
					float angle = m_globalTime * 0.8f + i * 0.5f;
					float ca = std::cos(angle), sa = std::sin(angle);
					ImVec2 proj[8];
					float corners[8][3] = {
						{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},
						{-1, 1,-1},{1, 1,-1},{1, 1,1},{-1, 1,1},
					};
					for (int v = 0; v < 8; v++) {
						float rx = corners[v][0]*ca - corners[v][2]*sa;
						float rz = corners[v][0]*sa + corners[v][2]*ca;
						float ry = corners[v][1];
						proj[v] = {cx + (rx - rz) * sz * 0.5f,
						           cy - (rx + rz) * sz * 0.25f - ry * sz * 0.5f};
					}
					auto drawFace = [&](int a, int b, int c2, int d, float shade) {
						ImVec2 pts[] = {proj[a], proj[b], proj[c2], proj[d]};
						ImU32 col = IM_COL32(
							(int)(c.r*shade*255), (int)(c.g*shade*255), (int)(c.b*shade*255), 230);
						dl->AddConvexPolyFilled(pts, 4, col);
						dl->AddPolyline(pts, 4, IM_COL32(0,0,0,80), true, 1.0f);
					};
					drawFace(7, 6, 5, 4, 1.0f);
					float nx_r = ca + sa, nx_f = -sa + ca;
					if (nx_r > 0) drawFace(1, 2, 6, 5, 0.72f);
					else          drawFace(3, 0, 4, 7, 0.72f);
					if (nx_f > 0) drawFace(2, 3, 7, 6, 0.85f);
					else          drawFace(0, 1, 5, 4, 0.85f);
				}

				// Stack count (large, Roboto, bottom-right with shadow)
				if (itemCount > 1) {
					char buf[8]; snprintf(buf, sizeof(buf), "%d", itemCount);
					ImFont* bigFont = ImGui::GetIO().Fonts->Fonts.Size > 1
						? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
					float tx = sx + slotPx - 8.0f;
					float ty = sy + slotPx - 6.0f;
					// Shadow
					dl->AddText(bigFont, 26.0f, {tx - strlen(buf)*13.0f + 1.5f, ty - 22.0f},
						IM_COL32(0,0,0,200), buf);
					// Main text
					dl->AddText(bigFont, 26.0f, {tx - strlen(buf)*13.0f, ty - 23.0f},
						IM_COL32(255,255,255,240), buf);
				}
			}

			// Key label (top-left, small)
			char key[4]; snprintf(key, sizeof(key), "%d", (i + 1) % 10);
			dl->AddText(ImGui::GetFont(), 14.0f, {sx + 4, sy + 2},
				IM_COL32(140, 130, 110, 130), key);
		}
	}

	// Equipment/Inventory UI ([I] to toggle)
	if (pe->inventory) {
		m_equipUI.setModels(&m_models, &m_iconCache);
		m_equipUI.render(*pe->inventory, m_server->blockRegistry(),
			(float)m_window.width(), (float)m_window.height());
	}

	// Chest UI (opened by right-clicking a chest block)
	if (pe->inventory && m_chestUI.isOpen()) {
		Entity* chestE = m_server->getEntity(m_chestUI.chestEntityId());
		if (chestE && chestE->inventory) {
			m_chestUI.setModels(&m_models, &m_iconCache);
			m_chestUI.render(*pe->inventory, *chestE->inventory,
				m_server->blockRegistry(),
				(float)m_window.width(), (float)m_window.height());
		}
	}

	// FPS counter
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.5f);
	if (ImGui::Begin("##fps", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
		ImGui::Text("FPS: %.0f", m_currentFPS);
	}
	ImGui::End();

	// RTS selection box overlay
	if (m_camera.mode == CameraMode::RTS && m_gameplay.isBoxDragging()) {
		auto s = m_gameplay.boxStart();
		auto e = m_gameplay.boxEnd();
		float sw = m_window.width(), sh = m_window.height();
		float x0 = (std::min(s.x, e.x) + 1) * 0.5f * sw;
		float y0 = (1 - std::max(s.y, e.y)) * 0.5f * sh;
		float x1 = (std::max(s.x, e.x) + 1) * 0.5f * sw;
		float y1 = (1 - std::min(s.y, e.y)) * 0.5f * sh;

		ImDrawList* dl = ImGui::GetForegroundDrawList();
		dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
			IM_COL32(100, 200, 255, 200), 0, 0, 2.0f);
		dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
			IM_COL32(100, 200, 255, 40));
	}

	// RTS selected entity count
	if (m_camera.mode == CameraMode::RTS && !m_gameplay.selectedEntities().empty()) {
		ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.6f);
		if (ImGui::Begin("##selection", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			ImGui::Text("Selected: %zu units", m_gameplay.selectedEntities().size());
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "Right-click to move");
		}
		ImGui::End();
	}

	// ── Hotbar drag/drop interaction layer ──
	if (pe->inventory) {
		float ww = (float)m_window.width(), wh = (float)m_window.height();
		int hslots = Hotbar::SLOTS;
		float slotPx = 60.0f, gapPx = 4.0f, hpad = 8.0f;
		float totalW = hslots * (slotPx + gapPx) - gapPx;
		float startX = (ww - totalW) * 0.5f;
		float startY = wh - slotPx - 12.0f;

		ImGui::SetNextWindowPos({startX - hpad, startY - hpad});
		ImGui::SetNextWindowSize({totalW + 2*hpad, slotPx + 2*hpad});
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {hpad, hpad});
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {gapPx, 0.0f});
		if (ImGui::Begin("##hotbar_dd", nullptr,
			ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoBackground| ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {

			struct DragSlot { char itemId[64]; int slot; };

			for (int i = 0; i < hslots; i++) {
				if (i > 0) ImGui::SameLine(0.0f, gapPx);
				char bid[24]; snprintf(bid, sizeof(bid), "##hdd%d", i);
				ImGui::InvisibleButton(bid, {slotPx, slotPx});

				// Drag source — pick up item from this hotbar slot
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
					DragSlot ds{}; ds.slot = i;
					snprintf(ds.itemId, sizeof(ds.itemId), "%s", m_hotbar.get(i).c_str());
					ImGui::SetDragDropPayload("INV_SLOT", &ds, sizeof(ds));
					if (ds.itemId[0]) ImGui::Text("%s", ds.itemId); else ImGui::Text("(empty)");
					ImGui::EndDragDropSource();
				}

				// Drop target — receive item into slot i
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
						auto* ds = (const DragSlot*)pl->Data;
						std::string newId = ds->itemId;
						std::string oldId = m_hotbar.get(i);
						if (ds->slot >= 0) {
							// Hotbar ↔ hotbar swap (client-only alias)
							m_hotbar.set(ds->slot, oldId);
						}
						m_hotbar.set(i, newId);
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	// Goal text overlay — world-anchored label above each entity's lightbulb
	if (m_showGoalText) {
		glm::mat4 vp2 = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
		m_server->forEachEntity([&](Entity& e) {
			if (e.id() == m_server->localPlayerId()) return;
			if (!e.def().isLiving() || e.removed) return;

			float entityTop = e.def().collision_box_max.y;
			float bobY = std::sin(m_globalTime * 2.0f + e.id() * 0.7f) * 0.05f;
			glm::vec3 textPos = e.position + glm::vec3(0, entityTop + 0.6f + bobY, 0);
			glm::vec4 clip = vp2 * glm::vec4(textPos, 1.0f);
			if (clip.w <= 0.0f || clip.z <= 0.0f) return;
			float nx = clip.x / clip.w, ny = clip.y / clip.w;
			if (nx < -1.5f || nx > 1.5f || ny < -1.5f || ny > 1.5f) return;

			const auto pit = m_entityGoalPopTimer.find(e.id());
			const bool hasTimer = (pit != m_entityGoalPopTimer.end());
			if (!e.hasError && !hasTimer) return;

			const float visT   = hasTimer ? pit->second / 3.0f : 0.0f;
			const float burstT = hasTimer ? std::min(pit->second / 0.45f, 1.0f) : 0.0f;

			const char* label;
			glm::vec4   color;
			if (e.hasError) {
				label = e.goalText.empty() ? "ERROR" : e.goalText.c_str();
				color = {1.0f, 0.35f + 0.15f * burstT, 0.35f + 0.15f * burstT, 0.95f};
			} else if (!e.goalText.empty()) {
				label = e.goalText.c_str();
				color = {0.88f + 0.12f * burstT, 1.0f, 0.80f + 0.20f * burstT, visT};
			} else {
				const auto& bid = e.getProp<std::string>(Prop::BehaviorId, "");
				label = bid.empty() ? e.typeId().c_str() : bid.c_str();
				color = {0.5f, 0.5f, 0.5f, 0.35f};
			}

			const float scale = 0.80f + 0.30f * burstT;
			float textW = (float)std::strlen(label) * scale * 0.018f;
			m_text.drawText(label, nx - textW * 0.5f, ny, scale, color, aspect);
		});
	}

	// Floating text notifications (damage, pickups, heals)
	m_floatText.render(m_camera, aspect, m_camera.mode, m_text,
	                   m_gameplay.selectedEntities());

	m_ui.endFrame();

	// Screenshot request: check for trigger file (external diagnostic tool)
	{
		static float screenshotCheckTimer = 0;
		screenshotCheckTimer += dt;
		if (screenshotCheckTimer > 0.5f) {
			screenshotCheckTimer = 0;
			if (std::filesystem::exists("/tmp/modcraft_screenshot_request")) {
				std::filesystem::remove("/tmp/modcraft_screenshot_request");
				saveScreenshot();
			}
		}
	}
}

// ============================================================
// renderPlaying — orchestrates all rendering helpers
// ============================================================
void Game::renderPlaying(float dt, float aspect, bool skipImGui) {
	if (!m_server) { printf("[Game] renderPlaying: no server\n"); return; }
	Entity* pe = playerEntity();
	if (!pe) { printf("[Game] renderPlaying: no player entity\n"); return; }

	renderWorld(dt, aspect);
	renderEntities(dt, aspect);
	renderHUD(dt, aspect, skipImGui);
}

} // namespace modcraft

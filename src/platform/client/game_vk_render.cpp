#include "client/game_vk.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "client/box_model_flatten.h"
#include "client/model_loader.h"
#include "client/network_server.h"
#include "client/raycast.h"
#include "net/server_interface.h"
#include "logic/action.h"
#include "logic/inventory.h"
#include "logic/material_values.h"
#include "agent/agent_client.h"

namespace civcraft::vk {

glm::mat4 Game::viewProj() const {
	glm::mat4 proj = m_cam.projectionMatrix(m_aspect);
	proj[1][1] *= -1.0f;  // Vulkan Y-flip
	glm::mat4 view = m_cam.viewMatrix();
	// Camera shake as a post-view translation — shadow pass stays stable.
	if (m_cameraShake > 0.0f && m_shakeIntensity > 0.0f) {
		float t = m_wallTime * 60.0f;
		float sx = std::sin(t * 13.1f) * 0.5f + std::sin(t * 27.3f) * 0.5f;
		float sy = std::cos(t * 17.7f) * 0.5f + std::cos(t * 31.9f) * 0.5f;
		float k = m_cameraShake * m_shakeIntensity;
		glm::mat4 jitter = glm::translate(glm::mat4(1.0f),
			glm::vec3(sx * k, sy * k, 0.0f));
		view = jitter * view;
	}
	return proj * view;
}

bool Game::projectWorld(const glm::vec3& world, glm::vec3& out) const {
	glm::vec4 clip = viewProj() * glm::vec4(world, 1.0f);
	if (clip.w <= 0.01f) return false;
	float ndcX = clip.x / clip.w;
	float ndcY = -clip.y / clip.w;  // Y already flipped in proj — un-flip
	// ^ but our UI is in OpenGL NDC (+y up) while VP flipped — rhi_ui.cpp
	// takes +y up directly. Internal VP has proj[1][1] *= -1 to handle VK
	// image coords, so re-flip here for UI consumption.
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return false;
	out = glm::vec3(ndcX, ndcY, clip.z / clip.w);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Rendering — world, entities, effects, HUD
// ─────────────────────────────────────────────────────────────────────────

namespace {

// Goal-keyword → animation-clip map. Mirror of client/entity_drawer.cpp's
// pickClip — kept identical so VK and GL mobs play the same clips for the
// same Python-defined behaviors. Modders extending goalText don't need to
// touch this file; they add the clip to the model's Python definition.
const char* pickClip(const std::string& goal) {
	if (goal.find("Chopping")   != std::string::npos) return "chop";
	if (goal.find("Mining")     != std::string::npos) return "mine";
	if (goal.find("Sleeping")   != std::string::npos) return "sleep";
	if (goal.find("Depositing") != std::string::npos) return "wave";
	if (goal.find("Dancing")    != std::string::npos) return "dance";
	if (goal.find("Flying")     != std::string::npos) return "fly";
	if (goal.find("Landing")    != std::string::npos) return "land";
	return "";
}

// Fill an AnimState for a remote mob and return its body yaw. Living entities
// get head tracking (±45° cap, overflow rolls into body yaw); non-living
// entities face e.yaw and don't track the head. Matches EntityDrawer::draw.
void fillMobAnim(const civcraft::Entity& e, float globalTime,
                 civcraft::AnimState& anim, float& bodyYawOut) {
	float speed = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
	anim.walkDistance = e.getProp<float>(civcraft::Prop::WalkDistance, 0.0f);
	anim.speed        = speed;
	anim.time         = globalTime;
	anim.currentClip  = pickClip(e.goalText);

	bodyYawOut = e.yaw;
	if (e.def().isLiving()) {
		constexpr float kHeadYawMax = 45.0f;
		float rel = e.lookYaw - e.yaw;
		while (rel >  180.0f) rel -= 360.0f;
		while (rel < -180.0f) rel += 360.0f;
		float headDeg = glm::clamp(rel, -kHeadYawMax, kHeadYawMax);
		bodyYawOut    = e.yaw + (rel - headDeg);
		anim.lookYaw   = glm::radians(-headDeg);
		anim.lookPitch = glm::radians(glm::clamp(e.lookPitch, -45.0f, 45.0f));
	}
}

} // namespace


void Game::renderWorld(float wallTime) {
	// Sun trajectory driven by server worldTime (Rule 3 — server is the sole
	// owner of time-of-day). worldTime ∈ [0,1): 0=midnight, 0.25=dawn,
	// 0.5=noon, 0.75=dusk. Angle convention puts the sun overhead at noon.
	float tod      = m_server ? m_server->worldTime() : 0.5f;
	float sunAngle = tod * 6.2831853f - 1.5707963f;   // 2π·tod − π/2
	glm::vec3 sunDir = glm::normalize(glm::vec3(
		std::cos(sunAngle),
		std::sin(sunAngle),
		0.35f));                                       // slight lateral offset for variety
	// sunStr: 0 at deep night, 1 at full day. Smooth ramp across the horizon.
	float sunStr = glm::smoothstep(-0.10f, 0.22f, sunDir.y);

	// Shadow pass (terrain + entities share the same depth map).
	auto* me = playerEntity();
	glm::vec3 pPos = me ? me->position : m_cam.position;
	glm::vec3 shadowCenter(pPos.x, 6.0f, pPos.z);
	glm::vec3 lightPos = shadowCenter + sunDir * 80.0f;
	glm::mat4 lightView = glm::lookAt(lightPos, shadowCenter,
		std::abs(sunDir.y) > 0.98f ? glm::vec3(0,0,1) : glm::vec3(0,1,0));
	glm::mat4 lightProj = glm::ortho(-60.0f, 60.0f, -60.0f, 60.0f, 1.0f, 200.0f);
	glm::mat4 shadowVP = lightProj * lightView;

	// Visual player Y — during a climb event, interpolate from fromY to toY with
	// an ease-out curve so the body rises smoothly instead of snapping a full
	// block in one frame. Once the climb animation completes, use the real
	// position. Falling / walking on flat ground always sees real Y.
	auto visualPlayerY = [&](float rawY) -> float {
		if (!m_climb.active()) return rawY;
		float u = glm::clamp(m_climb.t / m_climb.duration, 0.0f, 1.0f);
		u = 1.0f - (1.0f - u) * (1.0f - u);  // ease-out quad
		return m_climb.fromY + (m_climb.toY - m_climb.fromY) * u;
	};

	// Build entity box stream once per frame — shared by shadow + lit passes.
	// Each box is 19 floats: {mat4 model, r, g, b}. Uses the same Python
	// BoxModel definitions the GL client does (model_loader::loadAllModels),
	// flattened through civcraft::appendBoxModel — walk/idle/clip/head-track
	// animation runs in the same place for both backends.
	auto& charBoxes = m_scratch.charBoxes;
	charBoxes.clear();

	// Model-key resolution — character_skin prop wins; otherwise EntityDef.model
	// (stripped of its .py extension) with deterministic variant selection from
	// the entity id. Matches GL's resolveModelKey in game_render.cpp.
	auto resolveModelKey = [this](const civcraft::Entity& e) -> std::string {
		std::string skin = e.getProp<std::string>("character_skin", std::string{});
		std::string key;
		if (!skin.empty()) {
			auto colon = skin.find(':');
			key = (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
		} else {
			key = e.def().model;
			auto dot = key.rfind('.');
			if (dot != std::string::npos) key = key.substr(0, dot);
		}
		int n = civcraft::model_loader::countVariants(m_models, key);
		if (n > 0) {
			uint64_t h = (uint64_t)e.id() * 2654435761u;
			return key + "#" + std::to_string((int)(h % (uint64_t)n));
		}
		return key;
	};

	auto resolveItemModel = [this](const std::string& itemId)
	    -> const civcraft::BoxModel* {
		if (itemId.empty()) return nullptr;
		std::string key = itemId;
		auto colon = key.find(':');
		if (colon != std::string::npos) key = key.substr(colon + 1);
		auto it = m_models.find(key);
		return (it != m_models.end()) ? &it->second : nullptr;
	};

	// Local player body — skip in FPS so the body doesn't eclipse the camera.
	if (me && m_cam.mode != civcraft::CameraMode::FirstPerson) {
		auto pit = m_models.find(resolveModelKey(*me));
		if (pit != m_models.end()) {
			civcraft::AnimState anim{};
			anim.walkDistance = m_walkDist;
			anim.speed        = glm::length(glm::vec2(me->velocity.x,
			                                          me->velocity.z));
			anim.time         = m_wallTime;

			// Resolve held items: hotbar selected → main hand; offhand equip
			// slot → opposite (or chosen) hand. Matches GL game_render.cpp.
			civcraft::HeldItems held;
			int slot = m_hotbarSlot;
			std::string mainItemId = m_hotbar.get(slot);
			if (!mainItemId.empty() && me->inventory
			    && m_hotbar.count(slot, *me->inventory) <= 0) {
				mainItemId.clear();
			}
			std::string offhandItemId = me->inventory
			    ? me->inventory->equipped(civcraft::WearSlot::Offhand)
			    : std::string{};
			bool offhandRight = me->inventory
			    && me->inventory->offhandInRightHand();

			civcraft::HeldItem mainItem;
			mainItem.model = resolveItemModel(mainItemId);
			civcraft::HeldItem offItem;
			offItem.model = resolveItemModel(offhandItemId);
			if (offhandRight) {
				held.rightHand = offItem;
				held.leftHand  = mainItem;
			} else {
				held.rightHand = mainItem;
				held.leftHand  = offItem;
			}

			glm::vec3 bodyPos(me->position.x, visualPlayerY(me->position.y),
			                  me->position.z);
			civcraft::appendBoxModel(charBoxes, pit->second, bodyPos,
			                         glm::degrees(m_playerBodyYaw),
			                         anim, &held);
		}
	}

	// Remote entities — dispatch by EntityKind:
	//   Structure → skip (chunk mesher already owns the geometry)
	//   Item      → Python model if available, height-normalized to ~0.35
	//               blocks; fallback to a colored cube (block-drop case).
	//   Living    → Python model via resolveModelKey + fillMobAnim.
	{
		EntityId myId = m_server->localPlayerId();
		auto hasActiveAnim = [this](civcraft::EntityId eid) {
			for (const auto& a : m_pickupAnims)
				if (a.itemId == eid) return true;
			return false;
		};
		m_server->forEachEntity([&](civcraft::Entity& e) {
			if (e.id() == myId) return;
			const auto& def = e.def();
			if (def.isStructure()) return;
			if (def.isItem()) {
				if (hasActiveAnim(e.id())) return;
				std::string itemType =
				    e.getProp<std::string>(civcraft::Prop::ItemType);
				std::string modelKey = itemType;
				auto colon = modelKey.find(':');
				if (colon != std::string::npos)
					modelKey = modelKey.substr(colon + 1);

				unsigned h = (unsigned)e.id() * 2654435761u;
				float bob    = std::sin(m_wallTime * 2.5f + e.id() * 1.7f) * 0.06f;
				float bounce = std::abs(std::sin(m_wallTime * 4.0f
				                                 + e.id() * 2.3f)) * 0.04f;
				float ox = ((h & 0xFF) / 255.0f - 0.5f) * 0.3f;
				float oz = (((h >> 8) & 0xFF) / 255.0f - 0.5f) * 0.3f;
				float spinYawDeg = m_wallTime * 90.0f + e.id() * 47.0f;

				auto imIt = m_models.find(modelKey);
				if (imIt != m_models.end()) {
					// Height-normalize to ~0.35 blocks so drops read as pickups.
					civcraft::BoxModel m = imIt->second;
					float mh = std::max(m.totalHeight * m.modelScale, 0.1f);
					float worldScale = 0.35f / mh;
					for (auto& part : m.parts) {
						part.offset *= worldScale;
						part.halfSize *= worldScale;
					}
					civcraft::AnimState anim{};
					anim.time = m_wallTime;
					civcraft::appendBoxModel(charBoxes, m,
					    e.position + glm::vec3(ox, bob + bounce + 0.3f, oz),
					    spinYawDeg, anim);
				} else {
					const auto* bdef = m_server->blockRegistry().find(itemType);
					glm::vec3 color = bdef ? bdef->color_top
					                       : glm::vec3(0.8f, 0.5f, 0.2f);
					glm::vec3 size  = def.collision_box_max
					                - def.collision_box_min;
					glm::vec3 foot  = def.collision_box_min;
					glm::vec3 center = e.position + foot + size * 0.5f
					                 + glm::vec3(ox, bob + bounce, oz);
					civcraft::emitAABox(charBoxes,
					                    center - size * 0.5f, size, color);
				}
				return;
			}
			if (!def.isLiving()) return;   // unknown kind — don't guess

			std::string mkey = resolveModelKey(e);
			auto mit = m_models.find(mkey);
			if (mit == m_models.end()) return;

			civcraft::AnimState anim{};
			float bodyYaw;
			fillMobAnim(e, m_wallTime, anim, bodyYaw);
			civcraft::appendBoxModel(charBoxes, mit->second,
			                         e.position, bodyYaw, anim);
		});

		// Fly-to-player arc for claimed items — lerp from spawn point to the
		// picker's chest height with smoothstep, shrink as it approaches.
		glm::vec3 target = me ? me->position + glm::vec3(0, 0.8f, 0)
		                      : m_cam.position;
		for (const auto& a : m_pickupAnims) {
			if (a.duration <= 0.0f) continue;
			float u = glm::clamp(a.t / a.duration, 0.0f, 1.0f);
			float ease = u * u * (3.0f - 2.0f * u);
			glm::vec3 drawPos = glm::mix(a.startPos, target, ease);
			float scale = 1.0f - ease * 0.5f;
			glm::vec3 size(0.3f * scale);
			if (auto* ie = m_server->getEntity(a.itemId)) {
				glm::vec3 s = ie->def().collision_box_max
				            - ie->def().collision_box_min;
				size = s * scale;
			}
			civcraft::emitAABox(charBoxes,
			                    drawPos - size * 0.5f, size, a.color);
		}
	}
	uint32_t charBoxCount = (uint32_t)(charBoxes.size() / 19);

	// Shadow pass: every chunk casts via the chunk-mesh shadow pipeline,
	// then characters via the box-shadow pipeline. All three pipelines
	// (voxel/box/chunk) accumulate into the same depth map. Skip at night
	// — a sun below the horizon projects shadows from underground.
	if (sunStr > 0.05f) {
		for (const auto& kv : m_chunkMeshes) {
			if (kv.second != rhi::IRhi::kInvalidMesh)
				m_rhi->renderShadowsChunkMesh(&shadowVP[0][0], kv.second);
		}
		m_rhi->renderBoxShadows(&shadowVP[0][0], charBoxes.data(), charBoxCount);
	}

	// Sky — sun direction, strength, and timeSec drive the procedural shader
	// (LUT-driven zenith/horizon, sunrise bleed, stars + moon at night).
	glm::mat4 vp = viewProj();
	glm::mat4 invVP = glm::inverse(vp);
	// Zenith/horizon stubs are kept in the signature for the GL backend which
	// consumes them directly; VK derives all colors in-shader from sunStr.
	const float skyColor[3]     = { 0.50f, 0.70f, 0.95f };
	const float horizonColor[3] = { 0.85f, 0.78f, 0.65f };
	// Pass worldTime (in "day units") as the shader's animated phase — drives
	// star twinkle and cloud drift. Using server time (not wallTime) keeps
	// cloud motion consistent across all connected clients.
	float skyTime = tod * 24.0f;  // hours since midnight, purely for animation phase
	m_rhi->drawSky(&invVP[0][0], skyColor, horizonColor, &sunDir.x,
	               sunStr, skyTime);

	// Terrain — one drawChunkMeshOpaque per loaded chunk. The mesher
	// already trimmed hidden faces / applied AO + per-face shade, so this
	// is dramatically less geometry than the old per-voxel instancing.
	rhi::IRhi::SceneParams scene{};
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = m_cam.position;
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;
	scene.sunDir[0] = sunDir.x; scene.sunDir[1] = sunDir.y; scene.sunDir[2] = sunDir.z;
	scene.sunStr = sunStr;
	// Fog tracks the sky's horizon color so distant geometry dissolves into
	// the actual horizon tint, not a mismatched cold blue. sunStr drives the
	// warm→cool blend (dawn/dusk pushes toward peach; overcast→deep blue).
	// A distinct "deep-night" palette kicks in below sunStr≈0 so distant
	// geometry reads as black/indigo, matching the starfield overhead.
	glm::vec3 fogNight{0.025f, 0.035f, 0.082f};  // matches horizonNight
	glm::vec3 fogDawn {0.920f, 0.490f, 0.320f};  // matches horizonDawn (warm peach)
	glm::vec3 fogDay  {0.360f, 0.620f, 0.920f};  // matches horizonDay (blue, not white)
	float dayBlend  = glm::smoothstep(0.15f, 0.70f, sunStr);
	float dawnBlend = glm::smoothstep(0.00f, 0.35f, sunStr);
	glm::vec3 fogMix = glm::mix(glm::mix(fogNight, fogDawn, dawnBlend), fogDay, dayBlend);

	// Weather override: rain / snow desaturate toward a flat overcast palette
	// AND pull the fog in so distant terrain dissolves into the storm. leaves
	// is purely decorative — no fog change.
	float fogNear = 140.0f, fogFar = 320.0f;
	if (m_server) {
		const std::string& wk = m_server->weatherKind();
		float wi = glm::clamp(m_server->weatherIntensity(), 0.0f, 1.0f);
		if (wk == "rain") {
			glm::vec3 overcast{0.34f, 0.36f, 0.40f};
			fogMix = glm::mix(fogMix, overcast, 0.75f * wi);
			fogFar  = glm::mix(fogFar,  90.0f,  wi);
			fogNear = glm::mix(fogNear, 40.0f,  wi);
		} else if (wk == "snow") {
			glm::vec3 snowy{0.78f, 0.82f, 0.88f};
			fogMix = glm::mix(fogMix, snowy, 0.60f * wi);
			fogFar  = glm::mix(fogFar,  80.0f,  wi);
			fogNear = glm::mix(fogNear, 30.0f,  wi);
		}
	}
	float fogColor[3] = { fogMix.x, fogMix.y, fogMix.z };
	for (const auto& kv : m_chunkMeshes) {
		if (kv.second != rhi::IRhi::kInvalidMesh)
			m_rhi->drawChunkMeshOpaque(scene, fogColor, fogNear, fogFar, kv.second);
	}

	// Entities
	m_rhi->drawBoxModel(scene, charBoxes.data(), charBoxCount);

	// ── Block highlight — wireframe outline on targeted block ───────────
	// Raycast from camera and draw 12 thin dark boxes for the cube edges.
	// Matches GL renderHighlight dual-pass outline style.
	{
		glm::vec3 rayEye = m_cam.position;
		glm::vec3 rayDir = m_cam.front();
		if (m_cam.mode == civcraft::CameraMode::RPG ||
		    m_cam.mode == civcraft::CameraMode::RTS) {
			double mx, my;
			glfwGetCursorPos(m_window, &mx, &my);
			int ww = m_fbW, wh = m_fbH;
			if (ww > 0 && wh > 0) {
				float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
				float ndcY = 1.0f - (float)(my / wh) * 2.0f;
				glm::mat4 invVPhl = glm::inverse(vp);
				glm::vec4 nearW = invVPhl * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVPhl * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				rayDir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
			}
		}
		auto hlHit = civcraft::raycastBlocks(m_server->chunks(), rayEye, rayDir, 6.0f);
		if (hlHit && !m_uiWantsCursor) {
			glm::vec3 bp = glm::vec3(hlHit->blockPos);
			float eh = 0.005f;    // edge half-thickness
			float in = -0.002f;   // inset to avoid z-fighting
			glm::vec3 col(0.15f, 0.15f, 0.15f);
			glm::vec3 len(1.0f - 2*in, eh * 2, eh * 2);   // X-axis edge
			glm::vec3 tallY(eh * 2, 1.0f - 2*in, eh * 2); // Y-axis edge
			glm::vec3 tallZ(eh * 2, eh * 2, 1.0f - 2*in); // Z-axis edge
			auto& hl = m_scratch.hlBoxes;
			hl.clear();
			auto push = [&](glm::vec3 corner, glm::vec3 size) {
				civcraft::emitAABox(hl, corner, size, col);
			};
			// 4 edges along X (bottom + top rails)
			push({bp.x+in,   bp.y+in-eh, bp.z+in-eh},   len);
			push({bp.x+in,   bp.y+in-eh, bp.z+1-in-eh}, len);
			push({bp.x+in,   bp.y+1-in-eh, bp.z+in-eh}, len);
			push({bp.x+in,   bp.y+1-in-eh, bp.z+1-in-eh}, len);
			// 4 edges along Y (vertical posts)
			push({bp.x+in-eh, bp.y+in, bp.z+in-eh}, tallY);
			push({bp.x+1-in-eh, bp.y+in, bp.z+in-eh}, tallY);
			push({bp.x+in-eh, bp.y+in, bp.z+1-in-eh}, tallY);
			push({bp.x+1-in-eh, bp.y+in, bp.z+1-in-eh}, tallY);
			// 4 edges along Z
			push({bp.x+in-eh,   bp.y+in-eh,   bp.z+in}, tallZ);
			push({bp.x+1-in-eh, bp.y+in-eh,   bp.z+in}, tallZ);
			push({bp.x+in-eh,   bp.y+1-in-eh, bp.z+in}, tallZ);
			push({bp.x+1-in-eh, bp.y+1-in-eh, bp.z+in}, tallZ);
			m_rhi->drawBoxModel(scene, hl.data(),
			                    (uint32_t)(hl.size() / 19));
		}
	}

	// Door swing animations — hinged-panel sweep over the chunk mesh pipeline.
	if (!m_doorAnims.empty()) {
		constexpr float kDuration = 0.25f;
		auto& dverts = m_scratch.doorVerts;
		dverts.clear();
		auto pushVert = [&](glm::vec3 p, glm::vec3 c, glm::vec3 n, float shade) {
			dverts.push_back(p.x); dverts.push_back(p.y); dverts.push_back(p.z);
			dverts.push_back(c.x); dverts.push_back(c.y); dverts.push_back(c.z);
			dverts.push_back(n.x); dverts.push_back(n.y); dverts.push_back(n.z);
			dverts.push_back(1.0f);   // ao
			dverts.push_back(shade);
			dverts.push_back(1.0f);   // alpha — opaque
			dverts.push_back(0.0f);   // glow
		};
		for (const auto& a : m_doorAnims) {
			float t = std::min(a.timer / kDuration, 1.0f);
			t = t * t * (3.0f - 2.0f * t);   // smoothstep
			float theta = a.opening ? (t * (float)M_PI * 0.5f)
			                        : ((1.0f - t) * (float)M_PI * 0.5f);
			float fx = (float)a.basePos.x;
			float fy = (float)a.basePos.y;
			float fz = (float)a.basePos.z;
			float hh = (float)a.height;
			float pivX, pivZ, farX0, farZ0;
			if (!a.hingeRight) {
				pivX = fx; pivZ = fz;
				farX0 = fx + std::cos(theta);
				farZ0 = fz + std::sin(theta);
			} else {
				pivX = fx + 1.0f; pivZ = fz;
				farX0 = fx + 1.0f - std::cos(theta);
				farZ0 = fz + std::sin(theta);
			}
			float nx = -(farZ0 - pivZ);
			float nz =  (farX0 - pivX);
			float nl = std::sqrt(nx*nx + nz*nz);
			if (nl > 0.001f) { nx /= nl; nz /= nl; }
			glm::vec3 norm{nx, 0.0f, nz};
			const float shade = 0.85f;
			glm::vec3 v0{pivX,  fy,    pivZ};
			glm::vec3 v1{pivX,  fy+hh, pivZ};
			glm::vec3 v2{farX0, fy+hh, farZ0};
			glm::vec3 v3{farX0, fy,    farZ0};
			// Front face CCW.
			pushVert(v0, a.color, norm, shade);
			pushVert(v1, a.color, norm, shade);
			pushVert(v2, a.color, norm, shade);
			pushVert(v0, a.color, norm, shade);
			pushVert(v2, a.color, norm, shade);
			pushVert(v3, a.color, norm, shade);
			// Back face (flipped winding + inverted normal).
			glm::vec3 bn{-norm.x, 0.0f, -norm.z};
			pushVert(v0, a.color, bn, shade);
			pushVert(v3, a.color, bn, shade);
			pushVert(v2, a.color, bn, shade);
			pushVert(v0, a.color, bn, shade);
			pushVert(v2, a.color, bn, shade);
			pushVert(v1, a.color, bn, shade);
		}
		uint32_t vc = (uint32_t)(dverts.size() / 13);
		if (vc > 0) {
			if (m_doorAnimMesh == rhi::IRhi::kInvalidMesh)
				m_doorAnimMesh = m_rhi->createChunkMesh(dverts.data(), vc);
			else
				m_rhi->updateChunkMesh(m_doorAnimMesh, dverts.data(), vc);
			if (m_doorAnimMesh != rhi::IRhi::kInvalidMesh)
				m_rhi->drawChunkMeshOpaque(scene, fogColor, 140.0f, 320.0f, m_doorAnimMesh);
		}
	}
}

void Game::renderEntities(float /*wallTime*/) {
	// Box-model rendering already happens inside renderWorld so terrain +
	// entities share scene params with no extra state bookkeeping.
}

void Game::renderEffects(float wallTime) {
	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = m_cam.position;
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;

	// Torch flames, embers, fireflies
	auto& particles = m_scratch.particles;
	particles.clear();
	auto pushP = [&](glm::vec3 p, float size, glm::vec3 rgb, float a) {
		particles.push_back(p.x); particles.push_back(p.y); particles.push_back(p.z);
		particles.push_back(size);
		particles.push_back(rgb.x); particles.push_back(rgb.y); particles.push_back(rgb.z);
		particles.push_back(a);
	};
	auto fract = [](float x) { return x - std::floor(x); };

	int torchIdx = 0;
	for (int tz = -18; tz <= 18; tz += 6) {
		for (int tx = -18; tx <= 18; tx += 6) {
			if (tx == 0 && tz == 0) continue;
			bool onPath = (std::abs(tx) <= 1 || std::abs(tz) <= 1
			            || std::abs(tx - tz) <= 1 || std::abs(tx + tz) <= 1);
			if (!onPath) continue;
			glm::vec3 torch((float)tx, 10.4f, (float)tz);
			for (int k = 0; k < 4; k++) {
				float ph = wallTime * 4.0f + (float)(torchIdx + k * 7) * 0.5f;
				float pulse = 0.75f + 0.35f * std::sin(ph) + 0.15f * std::sin(ph * 2.3f);
				glm::vec3 p = torch + glm::vec3(
					0.08f * std::sin(ph * 1.3f + k),
					0.15f * std::sin(ph * 0.7f) + 0.14f * k,
					0.08f * std::cos(ph * 1.7f + k));
				pushP(p, 0.55f * pulse,
					glm::vec3(4.5f, 2.0f + k*0.2f, 0.35f), 1.0f);
			}
			{
				float ph = wallTime * 5.0f + (float)torchIdx * 0.3f;
				float pulse = 0.8f + 0.2f * std::sin(ph * 1.7f);
				pushP(torch + glm::vec3(0, 0.05f * std::sin(ph), 0),
					0.30f * pulse, glm::vec3(5.0f, 4.2f, 1.5f), 1.0f);
			}
			for (int k = 0; k < 6; k++) {
				float seed = (float)(torchIdx * 6 + k);
				float life = fract(wallTime * 0.5f + seed * 0.173f);
				float driftX = std::sin(seed * 17.1f + wallTime * 0.8f) * 0.45f;
				float driftZ = std::cos(seed * 13.7f + wallTime * 0.9f) * 0.45f;
				glm::vec3 p = torch + glm::vec3(
					driftX * life, 0.25f + life * 3.5f, driftZ * life);
				float fade = life < 0.3f ? (life / 0.3f) : (1.0f - (life - 0.3f) / 0.7f);
				float size = 0.18f + life * 0.15f;
				float cool = life;
				pushP(p, size,
					glm::vec3(3.5f - cool*0.8f, 1.4f - cool*1.0f, 0.15f), fade);
			}
			torchIdx++;
		}
	}

	// Firefly sparks — warm gold + cool cyan-white
	for (int k = 0; k < 80; k++) {
		float seed = (float)k;
		float bx = std::sin(seed * 17.3f) * 12.0f;
		float bz = std::cos(seed * 23.1f) * 12.0f;
		float ph = wallTime * (0.25f + 0.12f * fract(seed * 0.137f)) + seed;
		float rad = 0.5f + 0.6f * fract(seed * 0.091f);
		glm::vec3 p(
			bx + std::sin(ph) * rad,
			9.0f + 2.2f * std::sin(seed * 11.5f + wallTime * 0.4f)
			     + 0.9f * std::cos(ph * 0.7f),
			bz + std::cos(ph * 1.1f) * rad);
		float twinkle = 0.3f + 0.7f * std::fabs(std::sin(wallTime * 2.2f + seed * 3.3f));
		glm::vec3 col = ((int)k & 1) == 0
			? glm::vec3(3.0f, 3.2f, 1.8f)
			: glm::vec3(1.4f, 2.6f, 3.5f);
		pushP(p, 0.32f, col, 0.80f * twinkle);
	}

	// Ambient dust motes on a cylinder around the camera.
	{
		glm::vec3 eyeP = m_cam.position;
		float windX = std::sin(wallTime * 0.10f) * 0.06f * wallTime;
		float windZ = std::cos(wallTime * 0.08f) * 0.05f * wallTime;
		for (int k = 0; k < 40; k++) {
			float seed = (float)k;
			float ang  = seed * 0.5234f + wallTime * 0.05f;
			float rad  = 6.0f + 10.0f * std::fmod(seed * 0.137f, 1.0f);
			float bob  = std::sin(wallTime * 0.8f + seed * 1.7f) * 0.4f;
			glm::vec3 p = eyeP + glm::vec3(
				std::cos(ang) * rad + windX,
				1.5f + 4.0f * std::fmod(seed * 0.091f, 1.0f) + bob,
				std::sin(ang) * rad + windZ);
			float twinkle = 0.30f + 0.20f * std::sin(wallTime * 1.5f + seed * 3.1f);
			pushP(p, 0.04f, glm::vec3(1.40f, 1.30f, 1.05f), twinkle);
		}
	}

	// ── Weather particles ─────────────────────────────────────────────
	// Rain streaks / snowflakes / drifting leaves, sampled in a cylinder
	// around the camera so they move with the player and only live near
	// the view frustum. Server-broadcast weather kind + intensity drive
	// count and palette; wind vector tilts the fall direction.
	if (m_server) {
		const std::string& wkind = m_server->weatherKind();
		float wi = glm::clamp(m_server->weatherIntensity(), 0.0f, 1.0f);
		glm::vec2 wind = m_server->weatherWind();
		glm::vec3 eyeP = m_cam.position;

		// Collision: skip particles whose world cell is a solid block. The
		// ChunkSource lookup is cheap (hash into loaded chunks) and prevents
		// raindrops from streaking through the floor at the cost of one
		// getBlock() call per particle.
		civcraft::ChunkSource& cs = m_server->chunks();
		const civcraft::BlockRegistry& reg = cs.blockRegistry();
		auto insideSolid = [&](const glm::vec3& p) -> bool {
			int bx = (int)std::floor(p.x);
			int by = (int)std::floor(p.y);
			int bz = (int)std::floor(p.z);
			civcraft::BlockId bid = cs.getBlock(bx, by, bz);
			if (bid == 0) return false;
			return reg.get(bid).solid;
		};

		// Cylinder extent: R horizontal, from eye.y+hiY down to eye.y+loY.
		auto addWeatherPart = [&](int count, float R, float hiY, float loY,
		                          float fallSpeed, glm::vec3 color, float size,
		                          float alpha, bool tumble) {
			float span = hiY - loY;
			float period = span / std::max(fallSpeed, 0.1f);
			for (int k = 0; k < count; k++) {
				float seed = (float)k;
				float ang  = std::fmod(seed * 2.3998f, 6.2831853f);
				float rad  = R * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
				float phase  = std::fmod(seed * 0.091f, 1.0f);
				float t      = std::fmod(wallTime / period + phase, 1.0f);
				float y      = hiY - t * span;
				// Wind: accumulated displacement grows with fall time, so a
				// particle near the ground has been blown further than one
				// just spawned near the top (matches real raindrop streaks).
				float wShift = t * period;
				float tumbleX = tumble
				              ? std::sin(wallTime * 1.3f + seed * 4.7f) * 0.6f
				              : 0.0f;
				float tumbleZ = tumble
				              ? std::cos(wallTime * 1.1f + seed * 3.1f) * 0.6f
				              : 0.0f;
				glm::vec3 p = eyeP + glm::vec3(
					std::cos(ang) * rad + wind.x * wShift + tumbleX,
					y,
					std::sin(ang) * rad + wind.y * wShift + tumbleZ);
				if (insideSolid(p)) continue;
				pushP(p, size, color, alpha);
			}
		};

		if (wkind == "rain" && wi > 0.01f) {
			int count = (int)(320.0f * wi);
			glm::vec3 col(0.55f, 0.70f, 0.95f);
			addWeatherPart(count, 22.0f, 20.0f, -4.0f, 22.0f, col,
			               0.05f, 0.55f * wi, /*tumble*/ false);
		} else if (wkind == "snow" && wi > 0.01f) {
			int count = (int)(240.0f * wi);
			glm::vec3 col(1.10f, 1.15f, 1.25f);
			addWeatherPart(count, 20.0f, 18.0f, -4.0f, 3.5f, col,
			               0.09f, 0.70f * wi, /*tumble*/ true);
		} else if (wkind == "leaves" && wi > 0.01f) {
			int count = (int)(140.0f * wi);
			// Warm autumn palette — split across three tints based on seed.
			float span   = 22.0f;
			float period = span / 2.0f;
			for (int k = 0; k < count; k++) {
				float seed = (float)k;
				int   tint = (int)std::fmod(seed * 7.17f, 3.0f);
				glm::vec3 col = (tint == 0) ? glm::vec3(2.2f, 1.1f, 0.25f)
				              : (tint == 1) ? glm::vec3(2.5f, 0.7f, 0.20f)
				                            : glm::vec3(1.8f, 1.4f, 0.45f);
				float ang  = std::fmod(seed * 2.3998f, 6.2831853f);
				float rad  = 18.0f * std::sqrt(std::fmod(seed * 0.137f, 1.0f));
				float phase = std::fmod(seed * 0.091f, 1.0f);
				float t     = std::fmod(wallTime / period + phase, 1.0f);
				float y     = 18.0f - t * span;
				float wShift = t * period;
				float tumbleX = std::sin(wallTime * 0.9f + seed * 3.7f) * 1.2f;
				float tumbleZ = std::cos(wallTime * 0.8f + seed * 2.3f) * 1.2f;
				glm::vec3 p = eyeP + glm::vec3(
					std::cos(ang) * rad + wind.x * wShift + tumbleX,
					y,
					std::sin(ang) * rad + wind.y * wShift + tumbleZ);
				if (insideSolid(p)) continue;
				pushP(p, 0.12f, col, 0.55f * wi);
			}
		}
	}

	uint32_t particleCount = (uint32_t)(particles.size() / 8);
	if (particleCount > 0) m_rhi->drawParticles(scene, particles.data(), particleCount);

	// ── Sword slash ribbons (one per active swing) ──────────────────────
	for (const auto& s : m_slashes) {
		float swingT = s.t / s.duration;    // 0..1
		// Arc from upper-right → forward → lower-left, relative to dir.
		constexpr int N = 16;
		auto& rbuf = m_scratch.ribbons;
		rbuf.clear();
		glm::vec3 dir = s.dir;
		glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0,1,0)));
		for (int i = 0; i < N; i++) {
			float age = (float)i / (float)(N - 1);    // 0 = head, 1 = oldest
			float u = swingT - age * 0.5f;
			if (u < -0.05f) continue;
			u = glm::clamp(u, 0.0f, 1.0f);
			float ang = (u - 0.5f) * 2.4f;
			float r = 1.6f;
			glm::vec3 p = s.center
				+ dir * (std::cos(ang) * 0.9f - 0.3f)
				+ right * std::sin(ang) * r
				+ glm::vec3(0, std::cos(ang) * 0.6f, 0);
			float width = glm::mix(0.9f, 0.10f, age);
			float env = swingT < 0.15f ? swingT / 0.15f
				: swingT > 0.8f ? (1.0f - swingT) / 0.2f : 1.0f;
			env = glm::clamp(env, 0.0f, 1.0f);
			float headGlow = std::pow(1.0f - age, 1.6f);
			glm::vec3 col = glm::mix(s.late, s.early, headGlow);
			if (age < 0.12f) col = glm::mix(col, glm::vec3(5,5,4.5f), 1.0f - age / 0.12f);
			float alpha = env * (0.25f + 0.75f * (1.0f - age));
			rbuf.push_back(p.x); rbuf.push_back(p.y); rbuf.push_back(p.z);
			rbuf.push_back(width);
			rbuf.push_back(col.x); rbuf.push_back(col.y); rbuf.push_back(col.z);
			rbuf.push_back(alpha);
		}
		uint32_t rc = (uint32_t)(rbuf.size() / 8);
		if (rc >= 2) m_rhi->drawRibbon(scene, rbuf.data(), rc);
	}

	// ── Block break progress overlay ────────────────────────────────────
	// Dark crack-mark particles on each face of the block, progressively
	// denser through 3 stages — matches the GL renderBreakProgress style.
	if (m_breaking.active && m_breaking.hits > 0) {
		float progress = (float)m_breaking.hits / 3.0f;
		glm::vec3 bpf = glm::vec3(m_breaking.target);

		// Crack positions in (u,v) face-local coords [0,1]. Each stage
		// adds more marks for a progressively shattered look.
		static const float cracks[][2] = {
			// Stage 1 (0–4): initial fracture from center
			{0.50f,0.52f}, {0.18f,0.85f}, {0.80f,0.22f}, {0.82f,0.70f}, {0.28f,0.73f},
			// Stage 2 (5–9): branches spread
			{0.25f,0.15f}, {0.05f,0.28f}, {0.60f,0.90f}, {0.45f,0.38f}, {0.90f,0.55f},
			// Stage 3 (10–17): dense fracture network
			{0.95f,0.10f}, {0.10f,0.05f}, {0.40f,0.95f}, {0.72f,0.95f},
			{0.15f,0.45f}, {0.85f,0.85f}, {0.35f,0.65f}, {0.65f,0.15f},
		};
		static const int stageEnd[] = {5, 10, 18};
		int stage = (progress < 0.34f) ? 0 : (progress < 0.67f) ? 1 : 2;
		int numMarks = stageEnd[stage];

		// Map (u,v) to 3D for each of 6 cube faces with slight offset
		// to avoid z-fighting with the block surface.
		auto facePoint = [](int face, float u, float v) -> glm::vec3 {
			switch (face) {
			case 0: return {u,     v,     1.002f};   // front  z=1
			case 1: return {1-u,   v,    -0.002f};   // back   z=0
			case 2: return {1.002f, v,    1-u};       // right  x=1
			case 3: return {-0.002f,v,    u};         // left   x=0
			case 4: return {u,     1.002f, v};        // top    y=1
			case 5: return {u,    -0.002f, 1-v};      // bottom y=0
			default: return {u, v, 0};
			}
		};

		auto& crackParts = m_scratch.crackParts;
		crackParts.clear();
		float alpha = 0.55f + progress * 0.35f;
		float size  = 0.03f + progress * 0.04f;
		for (int face = 0; face < 6; face++) {
			for (int m = 0; m < numMarks; m++) {
				glm::vec3 fp = facePoint(face, cracks[m][0], cracks[m][1]);
				glm::vec3 p = bpf + fp;
				crackParts.push_back(p.x); crackParts.push_back(p.y); crackParts.push_back(p.z);
				crackParts.push_back(size);
				crackParts.push_back(0.08f); crackParts.push_back(0.06f); crackParts.push_back(0.04f);
				crackParts.push_back(alpha);
			}
		}
		int total = numMarks * 6;
		m_rhi->drawParticles(scene, crackParts.data(), (uint32_t)total);
	}

	// ── Mining hit event particles (burst per swing) ────────────────────
	for (const auto& he : m_hitEvents) {
		float age = he.t / 0.4f;
		if (age > 1.0f) continue;
		int n = 8;
		auto& hitParts = m_scratch.hitParts;
		hitParts.clear();
		for (int i = 0; i < n; i++) {
			float seed = (float)i;
			float dx = std::sin(seed * 17.3f + he.pos.x) * 0.5f * age;
			float dy = std::cos(seed * 23.7f + he.pos.y) * 0.4f * age + 0.3f * age;
			float dz = std::sin(seed * 31.1f + he.pos.z) * 0.5f * age;
			glm::vec3 p = he.pos + glm::vec3(dx, dy, dz);
			float fade = 1.0f - age;
			float sz = 0.06f + 0.02f * (1.0f - age);
			hitParts.push_back(p.x); hitParts.push_back(p.y); hitParts.push_back(p.z);
			hitParts.push_back(sz);
			hitParts.push_back(he.color.x); hitParts.push_back(he.color.y); hitParts.push_back(he.color.z);
			hitParts.push_back(fade * 0.9f);
		}
		m_rhi->drawParticles(scene, hitParts.data(), (uint32_t)n);
	}

	// ── Move target marker: spinning triangle hovering above the target.
	// SC2/MOBA-style waypoint — 3 bright vertices forming an equilateral
	// triangle, rotating around Y, with faint edge particles tracing the
	// outline. Plus a pulsing down-arrow pointing at the clicked block.
	auto emitGoTriangle = [&](glm::vec3 target, glm::vec3 col, float scale) {
		glm::vec3 center = target + glm::vec3(0, 1.4f * scale, 0);
		float spin = wallTime * 2.5f;
		float r = 0.55f * scale;
		auto& spinParts = m_scratch.spinParts;
		auto push = [&](glm::vec3 p, float sz, glm::vec3 c, float a) {
			spinParts.push_back(p.x); spinParts.push_back(p.y); spinParts.push_back(p.z);
			spinParts.push_back(sz);
			spinParts.push_back(c.x); spinParts.push_back(c.y); spinParts.push_back(c.z);
			spinParts.push_back(a);
		};
		// 3 corner vertices (bright, larger)
		glm::vec3 verts[3];
		for (int i = 0; i < 3; i++) {
			float a = spin + (float)i * ((float)M_PI * 2.0f / 3.0f);
			verts[i] = center + glm::vec3(std::cos(a) * r, 0, std::sin(a) * r);
			push(verts[i], 0.10f * scale, col, 1.0f);
		}
		// Trace each edge with 5 faint particles so the triangle outline reads
		// even when the vertices are partially occluded by terrain.
		for (int i = 0; i < 3; i++) {
			glm::vec3 a = verts[i];
			glm::vec3 b = verts[(i + 1) % 3];
			for (int k = 1; k < 5; k++) {
				float t = (float)k / 5.0f;
				push(glm::mix(a, b, t), 0.035f * scale, col, 0.55f);
			}
		}
		// Down-arrow stem pointing at the target ground spot.
		float pulse = 0.6f + 0.4f * std::sin(wallTime * 5.0f);
		for (int k = 0; k < 4; k++) {
			float u = (float)k / 3.0f;
			glm::vec3 p = glm::mix(center, target + glm::vec3(0, 0.1f, 0), u);
			float sz = 0.05f * scale * (1.0f - u * 0.4f);
			push(p, sz, col, pulse * (1.0f - u * 0.6f));
		}
	};

	if (m_hasMoveOrder) {
		auto& spinParts = m_scratch.spinParts;
		spinParts.clear();
		emitGoTriangle(m_moveOrderTarget, glm::vec3(0.30f, 1.0f, 0.45f), 1.0f);
		m_rhi->drawParticles(scene, spinParts.data(),
			(uint32_t)(spinParts.size() / 8));
	}

	// RTS group waypoints — one triangle per active move order, plus a trail
	// of particles from each ordered unit to its target. Smaller + blue-tinted
	// so they read as "unit orders" distinct from the player's green move
	// marker. Always visible in RTS (not F3-gated) — issuing a move order
	// without seeing where units are heading is disorienting.
	if (m_cam.mode == civcraft::CameraMode::RTS && !m_moveOrders.empty()) {
		auto& spinParts = m_scratch.spinParts;
		spinParts.clear();
		glm::vec3 waypointCol(0.35f, 0.75f, 1.0f);
		for (const auto& [eid, mo] : m_moveOrders) {
			if (!mo.active) continue;
			emitGoTriangle(mo.target, waypointCol, 0.5f);
			// Trail of particles from the unit up to the triangle so it's
			// obvious which unit owns which target.
			if (auto* e = m_server->getEntity(eid)) {
				glm::vec3 from = e->position + glm::vec3(0, 1.2f, 0);
				glm::vec3 to   = mo.target + glm::vec3(0, 0.5f, 0);
				for (int k = 1; k < 10; k++) {
					glm::vec3 p = glm::mix(from, to, (float)k / 10.0f);
					spinParts.push_back(p.x); spinParts.push_back(p.y); spinParts.push_back(p.z);
					spinParts.push_back(0.05f);
					spinParts.push_back(waypointCol.x);
					spinParts.push_back(waypointCol.y);
					spinParts.push_back(waypointCol.z);
					spinParts.push_back(0.35f);
				}
			}
		}
		if (!spinParts.empty())
			m_rhi->drawParticles(scene, spinParts.data(),
				(uint32_t)(spinParts.size() / 8));
	}

	// FPS hand swing — idle pose at lower-right, thrusts forward+down on LMB.
	if (m_cam.mode == civcraft::CameraMode::FirstPerson) {
		glm::vec3 fwd   = glm::normalize(m_cam.front());
		glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
		glm::vec3 up    = glm::normalize(glm::cross(right, fwd));
		float fwdOff   = 0.55f;
		float rightOff = 0.35f;
		float downOff  = 0.30f;
		glm::vec3 col = m_handColor;
		if (m_handSwingT >= 0.0f) {
			float t = glm::clamp(m_handSwingT / kHandSwingDur, 0.0f, 1.0f);
			// Thrust curve: fast-out (0→1 by t=0.35), slow return (back to 0 by t=1).
			float thrust = (t < 0.35f)
				? (t / 0.35f)
				: (1.0f - (t - 0.35f) / 0.65f);
			thrust = thrust * thrust * (3.0f - 2.0f * thrust);   // smoothstep
			fwdOff   += 0.45f * thrust;
			downOff  += 0.10f * thrust;
			rightOff -= 0.10f * thrust;
			col = glm::mix(col, glm::vec3(1.0f, 0.95f, 0.85f), 0.15f * thrust);
		}
		// Climb reach — on a step-up, the hand lifts forward+up as if grabbing
		// the ledge, peaking at 40% through the climb and settling by the end.
		// Layered on top of any active swing; uses the horizontal climb
		// direction (m_climb.forward) instead of camera forward so strafing
		// climbs don't look weird.
		if (m_climb.active()) {
			float u = glm::clamp(m_climb.t / m_climb.duration, 0.0f, 1.0f);
			// Rise fast (0→1 by u=0.4), ease back to rest by u=1.
			float reach = (u < 0.4f) ? (u / 0.4f) : (1.0f - (u - 0.4f) / 0.6f);
			reach = reach * reach * (3.0f - 2.0f * reach);
			glm::vec3 climbFwd(m_climb.forward.x, 0.0f, m_climb.forward.y);
			if (glm::length(climbFwd) > 0.001f) climbFwd = glm::normalize(climbFwd);
			// Pull the hand out of the idle slot and up-forward to the ledge.
			fwdOff   += 0.25f * reach;
			downOff  -= 0.55f * reach;   // up (downOff is subtracted below)
			rightOff -= 0.20f * reach;
			// Bias the hand toward the actual motion direction so a side-step
			// climb shows the correct-side hand reaching.
			glm::vec3 biasFwd = fwd * (1.0f - reach) + climbFwd * reach;
			fwd = glm::normalize(biasFwd);
			right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
			up    = glm::normalize(glm::cross(right, fwd));
			col = glm::mix(col, glm::vec3(0.9f, 0.82f, 0.65f), 0.2f * reach);
		}
		glm::vec3 center = m_cam.position
			+ fwd   * fwdOff
			+ right * rightOff
			- up    * downOff;
		float s = 0.12f;
		auto& fh = m_scratch.fpsHand;
		fh.clear();
		civcraft::emitAABox(fh, center - glm::vec3(s * 0.5f),
		                    glm::vec3(s), col);
		m_rhi->drawBoxModel(scene, fh.data(), 1);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// HUD — lightbulbs, HP bars, hotbar, player HP, crosshair
// ─────────────────────────────────────────────────────────────────────────

namespace {
// Character cell sizes in rhi_ui.cpp: kCharWNdc=0.018, kCharHNdc=0.032.
// Keep values in sync — if rhi_ui.cpp changes, mirror here.
constexpr float kCharWNdc = 0.018f;
constexpr float kCharHNdc = 0.032f;
}

// ── Named HUD colors ────────────────────────────────────────────────────
static constexpr glm::vec4 kCrosshair   {1.0f, 1.0f, 1.0f, 0.65f};
static constexpr glm::vec4 kTypeLabel   {0.70f, 0.70f, 0.70f, 0.65f};

// Rarity tiers by material value (Diablo-inspired)
static glm::vec4 rarityColor(float matVal) {
	if (matVal >= 15.0f) return {1.0f, 0.65f, 0.15f, 1.0f};  // legendary gold
	if (matVal >= 10.0f) return {0.70f, 0.45f, 0.90f, 1.0f};  // epic purple
	if (matVal >=  5.0f) return {0.35f, 0.60f, 0.95f, 1.0f};  // rare blue
	if (matVal >=  3.0f) return {0.40f, 0.85f, 0.40f, 1.0f};  // uncommon green
	return                      {0.82f, 0.80f, 0.76f, 1.0f};  // common off-white
}

static std::string prettyItemName(const std::string& itemId, size_t maxLen = 0) {
	std::string name = itemId;
	auto colon = name.find(':');
	if (colon != std::string::npos) name = name.substr(colon + 1);
	for (auto& c : name) if (c == '_') c = ' ';
	if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
	if (maxLen > 0 && name.size() > maxLen) name = name.substr(0, maxLen - 1) + "~";
	return name;
}

// Render each hotbar slot's held item as a real 3D box-model anchored just in
// front of the camera. Items sit at d = 0.18 m (past the 0.1 m near plane) so
// depth-test always wins against world geo; their world-space size is sized
// from the slot's NDC footprint so they read as proper 3D inventory icons
// instead of flat rect swatches. Runs BEFORE renderHUD so slot borders/count
// badges drawn later (2D swapchain pass) still compose on top.
void Game::renderHotbarItems3D() {
	// The inventory panel sits above the hotbar (not over it), so keep
	// rendering 3D hotbar items even when Tab is open — otherwise the
	// hotbar slots fall back to flat 2D swatches mid-drag.
	civcraft::Entity* me = m_server->getEntity(m_server->localPlayerId());
	if (!me || !me->inventory) return;

	// Slot geometry — must match the renderHUD hotbar layout exactly.
	// Pixel-square slots: NDC width = NDC height / aspect so every slot
	// reads as a real square at any window aspect. h=0.14 lands around
	// 56 px on a 900p window (56/400 = 0.14); slotW follows from aspect.
	const int   N     = 10;
	const float h     = 0.14f;
	const float slotW = h / m_aspect;
	const float gap   = 0.008f;
	float totalW = N * slotW + (N - 1) * gap;
	float x0 = -totalW * 0.5f;
	float y0 = -0.97f;

	// Camera basis — items are placed in world-space just ahead of the camera
	// so the existing viewProj projects them into the slot region on screen.
	// Using camRight/camUp keeps the slot layout glued to screen NDC even as
	// the player looks around, and using camFwd*d puts items in front of all
	// world geometry.
	glm::vec3 camPos   = m_cam.position;
	glm::vec3 camFwd   = glm::normalize(m_cam.front());
	glm::vec3 worldUp  = glm::vec3(0, 1, 0);
	if (std::abs(glm::dot(camFwd, worldUp)) > 0.99f) worldUp = glm::vec3(0, 0, 1);
	glm::vec3 camRight = glm::normalize(glm::cross(camFwd, worldUp));
	glm::vec3 camUp    = glm::normalize(glm::cross(camRight, camFwd));

	// Distance 0.6 m keeps items past the near plane (0.1 m) while staying
	// closer than any world geo.
	const float d      = 0.6f;
	const float fovRad = glm::radians(m_cam.fov);
	const float halfH  = d * std::tan(fovRad * 0.5f);
	const float halfW  = halfH * m_aspect;

	auto& boxes = m_scratch.charBoxes;
	boxes.clear();

	for (int i = 0; i < N; i++) {
		std::string itemId = m_hotbar.get(i);
		if (itemId.empty()) continue;
		if (me->inventory->count(itemId) <= 0) continue;

		// Look up model — strip "base:" namespace prefix if present.
		std::string stem = itemId;
		auto colon = stem.find(':');
		if (colon != std::string::npos) stem = stem.substr(colon + 1);
		auto it = m_models.find(stem);
		if (it == m_models.end()) {
			it = m_models.find(itemId);
			if (it == m_models.end()) continue;
		}

		// Slot center in NDC (+y up).
		float slotCx = x0 + i * (slotW + gap) + slotW * 0.5f;
		float slotCy = y0 + h * 0.5f;
		glm::vec3 itemWorldPos = camPos + camFwd * d
		                       + camRight * (slotCx * halfW)
		                       + camUp    * (slotCy * halfH);

		// Target world extent for the model's longest AABB axis. The slot is
		// square in pixels, so the world-space slot height (h_ndc * halfH)
		// equals its world-space width (slotW_ndc * halfW). At the fixed
		// three-quarter yaw of 25°, a cube's horizontal projection spans
		// cos(25°) + sin(25°) ≈ 1.33× its side — so we divide by ~1.33 and
		// leave a small margin so the silhouette sits fully inside the
		// slot borders.
		float slotWorld    = h * halfH;            // = slotW * halfW
		float targetWorldH = slotWorld * 0.72f;    // ≈ 1 / 1.33 with padding

		civcraft::BoxModel m = it->second;

		// Scale so the model's longest AABB extent fills the slot — keeps
		// thin items (swords, potions) from appearing tiny next to blocks.
		float s0 = m.modelScale;
		float minX =  1e9f, maxX = -1e9f;
		float minY =  1e9f, maxY = -1e9f;
		float minZ =  1e9f, maxZ = -1e9f;
		for (auto& part : m.parts) {
			glm::vec3 o = part.offset   * s0;
			glm::vec3 hs= part.halfSize * s0;
			minX = std::min(minX, o.x - hs.x); maxX = std::max(maxX, o.x + hs.x);
			minY = std::min(minY, o.y - hs.y); maxY = std::max(maxY, o.y + hs.y);
			minZ = std::min(minZ, o.z - hs.z); maxZ = std::max(maxZ, o.z + hs.z);
		}
		float dy = std::max(maxY - minY, 0.01f);
		float dx = std::max(maxX - minX, 0.01f);
		float dz = std::max(maxZ - minZ, 0.01f);
		float maxDim = std::max(dy, std::max(dx, dz));
		float scale = targetWorldH / maxDim;
		for (auto& part : m.parts) {
			part.offset   *= scale;
			part.halfSize *= scale;
		}
		// appendBoxModel places parts at `feet + rotY(-yaw-90°) * offset`, so
		// the model rotates around its feet in world Y. Vertical centering
		// along Y survives the yaw spin (Y is the rotation axis); horizontal
		// offsets would orbit. Most models have parts symmetric about the
		// model's Y axis, so shifting by the Y-centroid alone is enough.
		float cy = (minY + maxY) * 0.5f * scale;
		glm::vec3 feet = itemWorldPos - glm::vec3(0.0f, cy, 0.0f);

		civcraft::AnimState anim{};
		anim.time = m_wallTime;
		// Fixed 25° yaw gives a three-quarter pose (shows front + right side)
		// so cubic blocks read as 3D without spinning — spinning under
		// perspective makes the corners swing toward the camera and grow
		// visibly, breaking slot boundaries.
		float spinYaw = 25.0f;

		civcraft::appendBoxModel(boxes, m, feet, spinYaw, anim);
	}

	if (boxes.empty()) return;

	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float) * 16);
	scene.camPos[0] = camPos.x; scene.camPos[1] = camPos.y; scene.camPos[2] = camPos.z;
	scene.time = m_wallTime;
	// Fixed key-light so inventory icons are lit consistently across time-of-day.
	glm::vec3 sun = glm::normalize(glm::vec3(0.35f, 0.90f, 0.45f));
	scene.sunDir[0] = sun.x; scene.sunDir[1] = sun.y; scene.sunDir[2] = sun.z;
	scene.sunStr = 0.95f;

	m_rhi->drawBoxModel(scene, boxes.data(), (uint32_t)(boxes.size() / 19));
}

void Game::renderHUD() {
	if (m_damageVignette > 0.0f) {
		float a = glm::clamp(m_damageVignette, 0.0f, 1.0f);
		float red[4] = { 0.85f, 0.08f, 0.08f, a * 0.35f };
		m_rhi->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, red);
		float bandA[4] = { 1.0f, 0.12f, 0.10f, a * 0.55f };
		m_rhi->drawRect2D(-1.0f,  0.75f, 2.0f, 0.25f, bandA);
		m_rhi->drawRect2D(-1.0f, -1.0f,  2.0f, 0.25f, bandA);
		m_rhi->drawRect2D(-1.0f, -1.0f,  0.18f, 2.0f, bandA);
		m_rhi->drawRect2D( 0.82f,-1.0f,  0.18f, 2.0f, bandA);
	}

	// Crosshair — screen-center in both FPS and TPS (over-the-shoulder
	// shooter style). Hidden in RPG/RTS (cursor is a world ray, not a
	// screen-center reticle).
	if (m_cam.mode == civcraft::CameraMode::FirstPerson ||
	    m_cam.mode == civcraft::CameraMode::ThirdPerson) {
		float cx = 0.0f, cy = 0.0f;
		// Hitmarker flash: orange on damage, red on kill shot
		glm::vec4 chColor = kCrosshair;
		if (m_hitmarkerTimer > 0) {
			float blend = m_hitmarkerTimer / 0.18f;
			glm::vec3 flash = m_hitmarkerKill
				? glm::vec3(1.0f, 0.15f, 0.05f)
				: glm::vec3(1.0f, 0.80f, 0.25f);
			chColor = glm::vec4(
				glm::mix(1.0f, flash.r, blend),
				glm::mix(1.0f, flash.g, blend),
				glm::mix(1.0f, flash.b, blend),
				0.9f + blend * 0.1f);
		}
		m_rhi->drawRect2D(cx - 0.003f, cy - 0.006f,  0.006f, 0.012f, &chColor.x);
		m_rhi->drawRect2D(cx - 0.010f, cy - 0.0015f, 0.020f, 0.003f, &chColor.x);
	}

	// ── Entity lightbulb + goal label + HP bar + type label ─────────────
	// Decorations (lightbulb/goal/HP/type) are for Living only — items and
	// structures don't have goals, HP, or AI state worth surfacing.
	{
		EntityId myId = m_server->localPlayerId();
		m_server->forEachEntity([&](civcraft::Entity& e) {
			if (e.id() == myId) return;
			if (!e.def().isLiving()) return;
			glm::vec3 anchor = e.position + glm::vec3(0, 2.1f, 0);
			glm::vec3 ndc;
			if (!projectWorld(anchor, ndc)) return;

			bool broken = e.goalText.find("\xE2\x9A\xA0") != std::string::npos;
			float pulse = 1.0f + 0.08f * std::sin(m_wallTime * 3.2f + e.id() * 0.9f);
			float scale = 1.5f * pulse;
			float gw = kCharWNdc * scale;
			float gx = ndc.x - gw * 0.5f;
			float tint[4];
			tint[0] = 1.0f;
			tint[1] = broken ? 0.25f : 1.0f;
			tint[2] = broken ? 0.25f : 0.85f;
			tint[3] = 1.0f;
			m_rhi->drawTitle2D("!", gx, ndc.y, scale, tint);

			std::string label = e.goalText.empty() ? std::string("…") : e.goalText;
			if (label.size() > 40) label = label.substr(0, 39) + "…";
			float rawW = label.size() * kCharWNdc;
			float maxW = 0.50f;
			float lScale = rawW > maxW ? std::max(0.55f, maxW / rawW) : 0.85f;
			float lW = rawW * lScale;
			float lX = ndc.x - lW * 0.5f;
			float lY = ndc.y + kCharHNdc * scale + 0.020f;
			m_rhi->drawText2D(label.c_str(), lX, lY, lScale, tint);

			int maxHp = e.def().max_hp > 0 ? e.def().max_hp : 100;
			float hpFrac = std::clamp((float)e.hp() / (float)maxHp, 0.0f, 1.0f);
			float barW = 0.10f, barH = 0.010f;
			float barX = ndc.x - barW * 0.5f;
			float barY = ndc.y - 0.020f;
			const float hpBg[4]   = {0.08f, 0.04f, 0.04f, 0.85f};
			const float hpGreen[4] = {0.20f, 0.78f, 0.28f, 1.0f};
			const float hpRed[4]   = {0.78f, 0.20f, 0.18f, 1.0f};
			m_rhi->drawRect2D(barX, barY, barW, barH, hpBg);
			auto& fill = hpFrac > 0.35f ? hpGreen : hpRed;
			m_rhi->drawRect2D(barX, barY, barW * hpFrac, barH, fill);

			float tyW = e.typeId().size() * kCharWNdc * 0.6f;
			float tyX = ndc.x - tyW * 0.5f;
			float tyY = barY - 0.018f;
			m_rhi->drawText2D(e.typeId().c_str(), tyX, tyY, 0.6f, &kTypeLabel.x);
		});
	}

	// ── Interaction prompts near aimed target ──────────────────────────
	// Contextual hint hovering above the block/entity the player is looking
	// at in FPS/TPS. Modern games surface the action to the target, not
	// just a static tutorial bar. Hidden if any UI owns the cursor.
	if (!m_uiWantsCursor &&
	    (m_cam.mode == civcraft::CameraMode::FirstPerson ||
	     m_cam.mode == civcraft::CameraMode::ThirdPerson)) {
		glm::vec3 eye = m_cam.position;
		glm::vec3 dir = m_cam.front();
		EntityId myId = m_server->localPlayerId();

		auto& ents = m_scratch.ents;
		ents.clear();
		m_server->forEachEntity([&](civcraft::Entity& e) {
			if (!e.def().isLiving()) return;
			ents.push_back({e.id(), e.typeId(), e.position,
				e.def().collision_box_min, e.def().collision_box_max,
				e.goalText, e.hasError});
		});
		auto eHit = civcraft::raycastEntities(ents, eye, dir, 12.0f, myId);
		auto bHit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 6.0f);

		glm::vec3 anchor;
		std::string prompt;
		glm::vec3 color{0.95f, 0.90f, 0.55f};
		if (eHit && (!bHit || eHit->distance <= bHit->distance)) {
			if (auto* e = m_server->getEntity(eHit->entityId))
				anchor = e->position + glm::vec3(0, 2.6f, 0);
			prompt = "[LMB] Attack";
			color = {1.0f, 0.55f, 0.45f};
		} else if (bHit) {
			glm::ivec3 bp = bHit->hasInteract ? bHit->interactPos : bHit->blockPos;
			const auto& bdef = m_server->blockRegistry().get(
				m_server->chunks().getBlock(bp.x, bp.y, bp.z));
			const std::string& sid = bdef.string_id;
			bool isChest   = sid.find("chest")  != std::string::npos;
			bool isDoor    = sid.find("door")   != std::string::npos;
			bool isButton  = sid.find("button") != std::string::npos;
			bool isLever   = sid.find("lever")  != std::string::npos;
			bool isTnt     = sid.find("tnt")    != std::string::npos;
			anchor = glm::vec3(bp) + glm::vec3(0.5f, 1.25f, 0.5f);
			if (isChest)       prompt = "[E] Open";
			else if (isDoor)   prompt = "[E] Toggle";
			else if (isButton) prompt = "[E] Press";
			else if (isLever)  prompt = "[E] Flip";
			else if (isTnt)    prompt = "[E] Ignite";
			else               prompt = "[LMB] Mine";
			if (isChest || isDoor || isButton || isLever || isTnt)
				color = {0.55f, 0.95f, 0.70f};
		}
		if (!prompt.empty()) {
			glm::vec3 ndc;
			if (projectWorld(anchor, ndc)) {
				float scale = 0.85f;
				float rawW = prompt.size() * kCharWNdc * scale;
				float x = ndc.x - rawW * 0.5f;
				float rgba[4] = { color.x, color.y, color.z, 0.92f };
				m_rhi->drawText2D(prompt.c_str(), x, ndc.y, scale, rgba);
			}
		}
	}

	// Notification stack — newest at bottom above the hotbar.
	{
		float pillW = 0.30f;
		float pillH = 0.035f;
		float gap   = 0.006f;
		float rightEdge = 0.96f;
		float baseY = -0.82f;
		int shown = 0;
		for (int i = (int)m_notifs.size() - 1; i >= 0; --i) {
			const auto& n = m_notifs[i];
			float u = n.t / n.lifetime;
			float fadeIn  = glm::clamp(n.t / 0.18f, 0.0f, 1.0f);
			float fadeOut = (u > 0.75f) ? (1.0f - (u - 0.75f) / 0.25f) : 1.0f;
			float alpha = glm::clamp(fadeIn * fadeOut, 0.0f, 1.0f);
			float y = baseY + shown * (pillH + gap);
			float x = rightEdge - pillW;
			float bg[4]    = { 0.05f, 0.04f, 0.06f, 0.78f * alpha };
			float accent[4] = { n.color.x, n.color.y, n.color.z, 0.95f * alpha };
			m_rhi->drawRect2D(x, y, pillW, pillH, bg);
			m_rhi->drawRect2D(x, y, 0.006f, pillH, accent);
			float txtC[4] = { n.color.x, n.color.y, n.color.z, alpha };
			m_rhi->drawText2D(n.text.c_str(),
				x + 0.014f, y + pillH * 0.5f - 0.010f, 0.65f, txtC);
			shown++;
			if (shown >= 6) break;
		}
	}

	// ── Floating damage numbers ─────────────────────────────────────────
	for (const auto& f : m_floaters) {
		float u = f.t / f.lifetime;
		glm::vec3 world = f.worldPos + glm::vec3(0, u * f.rise, 0);
		glm::vec3 ndc;
		if (!projectWorld(world, ndc)) continue;
		float alpha = 1.0f - u;
		float scale = 1.0f + 0.4f * (1.0f - u);  // pop then shrink
		float rawW = f.text.size() * kCharWNdc * scale;
		float x = ndc.x - rawW * 0.5f;
		float rgba[4] = { f.color.x, f.color.y, f.color.z, alpha };
		m_rhi->drawTitle2D(f.text.c_str(), x, ndc.y, scale, rgba);
	}

	// ── Player status panel (top-left) ──────────────────────────────────
	// Diablo-style: HP globe as a wide bar with gradient, coins as small
	// counter beneath. Dark glass panel background.
	{
		float px = -0.96f, py = 0.88f;
		float pw = 0.38f, ph = 0.10f;
		const float panelBg[4] = {0.04f, 0.03f, 0.05f, 0.82f};
		const float panelBdr[4] = {0.25f, 0.20f, 0.12f, 0.45f};
		m_rhi->drawRect2D(px, py, pw, ph, panelBg);
		m_rhi->drawRect2D(px, py, pw, 0.002f, panelBdr);       // top edge
		m_rhi->drawRect2D(px, py + ph - 0.002f, pw, 0.002f, panelBdr); // bot
		m_rhi->drawRect2D(px, py, 0.002f, ph, panelBdr);       // left
		m_rhi->drawRect2D(px + pw - 0.002f, py, 0.002f, ph, panelBdr); // right

		// HP bar — red fill with dark inner track
		auto* hpEntity = playerEntity();
		float playerHp = hpEntity ? (float)hpEntity->hp() : 0.0f;
		float hpFrac = std::clamp(playerHp / (float)kTune.playerMaxHP, 0.0f, 1.0f);
		float bx = px + 0.015f, by = py + 0.055f;
		float bw = pw - 0.030f, bh = 0.028f;
		const float hpTrack[4] = {0.08f, 0.04f, 0.04f, 0.90f};
		const float hpFill[4]  = {0.82f, 0.18f, 0.15f, 0.95f};
		const float hpGlow[4]  = {0.95f, 0.30f, 0.25f, 0.60f};
		m_rhi->drawRect2D(bx, by, bw, bh, hpTrack);
		m_rhi->drawRect2D(bx + 0.002f, by + 0.002f,
			(bw - 0.004f) * hpFrac, bh - 0.004f, hpFill);
		if (hpFrac > 0.0f)
			m_rhi->drawRect2D(bx + 0.002f, by + 0.002f,
				(bw - 0.004f) * hpFrac, 0.005f, hpGlow);

		char hpBuf[32];
		std::snprintf(hpBuf, sizeof(hpBuf), "%d / %d",
			(int)std::round(playerHp), kTune.playerMaxHP);
		const float hpTxt[4] = {1.0f, 0.92f, 0.88f, 1.0f};
		float hpTxtW = std::strlen(hpBuf) * kCharWNdc * 0.7f;
		m_rhi->drawText2D(hpBuf, bx + bw * 0.5f - hpTxtW * 0.5f,
			by + 0.005f, 0.7f, hpTxt);
		const float hpLabel[4] = {0.60f, 0.50f, 0.45f, 0.85f};
		m_rhi->drawText2D("HP", bx, by + bh + 0.004f, 0.55f, hpLabel);

		// Coins — small gold counter, top of panel
		if (m_coins > 0) {
			char coinBuf[32];
			std::snprintf(coinBuf, sizeof(coinBuf), "%d", m_coins);
			const float coinGold[4] = {1.0f, 0.78f, 0.25f, 0.95f};
			const float coinDim[4]  = {0.65f, 0.55f, 0.35f, 0.80f};
			m_rhi->drawText2D("Gold", px + 0.015f, py + 0.015f, 0.55f, coinDim);
			m_rhi->drawText2D(coinBuf, px + 0.085f, py + 0.015f, 0.65f, coinGold);
		}
	}

	// ── Hotbar (bottom-center) ──────────────────────────────────────────
	// Sleek dark glass with warm gold selection. All slots generic, filled
	// with real inventory items. Rarity-colored item names.
	// Pixel-square slots — keep these constants in sync with
	// renderHotbarItems3D() above.
	{
		const int   N     = 10;
		const float h     = 0.14f;
		const float slotW = h / m_aspect;
		const float gap   = 0.008f;
		float totalW = N * slotW + (N - 1) * gap;
		float x0 = -totalW * 0.5f;
		float y0 = -0.97f;

		// Panel background — dark glass with subtle warm border.
		// Alpha tuned down so 3D item models drawn behind this panel by
		// renderHotbarItems3D() remain clearly visible through the panel.
		const float panelBg[4]  = {0.03f, 0.025f, 0.04f, 0.38f};
		const float panelBdr[4] = {0.22f, 0.18f, 0.10f, 0.55f};
		float pad = 0.008f;
		m_rhi->drawRect2D(x0 - pad, y0 - pad, totalW + pad * 2, h + pad * 2, panelBg);
		m_rhi->drawRect2D(x0 - pad, y0 - pad, totalW + pad * 2, 0.002f, panelBdr);
		m_rhi->drawRect2D(x0 - pad, y0 + h + pad - 0.002f, totalW + pad * 2, 0.002f, panelBdr);

		const float slotEmptyBg[4] = {0.10f, 0.09f, 0.12f, 0.60f};  // full for empty
		const float slotItemBg[4]  = {0.10f, 0.09f, 0.12f, 0.18f};  // faint for filled
		const float selGlow[4]  = {0.95f, 0.75f, 0.25f, 0.90f};
		const float selInner[4] = {0.95f, 0.75f, 0.25f, 0.15f};
		const float keyDim[4]   = {0.50f, 0.48f, 0.55f, 0.70f};

		civcraft::Entity* me = m_server->getEntity(m_server->localPlayerId());

		for (int i = 0; i < N; i++) {
			float x = x0 + i * (slotW + gap);
			bool selected = (i == m_hotbarSlot);

			// Pick slot fill: faint for slots where a 3D item sits behind
			// (renderHotbarItems3D drew it) so the item reads through; fuller
			// for empty slots so they still look like placeholders.
			std::string probeId = me && me->inventory
			                    ? m_hotbar.get(i) : std::string();
			bool slotHasItem = !probeId.empty()
			                && me && me->inventory
			                && me->inventory->count(probeId) > 0;
			const float* slotBg = selected ? selInner
			                : (slotHasItem ? slotItemBg : slotEmptyBg);

			// Slot fill — subtle glow when selected
			m_rhi->drawRect2D(x, y0, slotW, h, slotBg);

			// Selection border — thin gold edges
			if (selected) {
				float t = 0.003f;
				m_rhi->drawRect2D(x, y0, slotW, t, selGlow);
				m_rhi->drawRect2D(x, y0 + h - t, slotW, t, selGlow);
				m_rhi->drawRect2D(x, y0, t, h, selGlow);
				m_rhi->drawRect2D(x + slotW - t, y0, t, h, selGlow);
			}

			// Item content — slot→itemId alias comes from m_hotbar (client
			// rearrange layer), resolved against live inventory count.
			std::string itemId = (me && me->inventory) ? m_hotbar.get(i) : std::string();
			int count = (!itemId.empty() && me && me->inventory) ? me->inventory->count(itemId) : 0;
			if (count > 0) {
				std::string rawId = itemId;
				auto colon = rawId.find(':');
				if (colon != std::string::npos) rawId = rawId.substr(colon + 1);
				float matVal = civcraft::getMaterialValue(rawId);
				glm::vec4 rc = rarityColor(matVal);

				// 3D icon: renderHotbarItems3D() already drew the item's box
				// model just in front of the camera, projected into this slot.
				// The 2D swatch below is only drawn as a fallback when no
				// box-model exists for the item id (rare — every artifact
				// item.py comes with a model.py).
				bool hasModel = (m_models.find(rawId) != m_models.end())
				             || (m_models.find(itemId) != m_models.end());
				if (!hasModel) {
					// Block color_top when available, otherwise a deterministic
					// material-rarity tint. Drawn as a 3-tier iso-swatch.
					const civcraft::BlockDef* bdef = m_server->blockRegistry().find(itemId);
					glm::vec3 top, side, bot;
					if (bdef) {
						top  = bdef->color_top;
						side = bdef->color_side;
						bot  = bdef->color_bottom;
					} else {
						size_t hs = std::hash<std::string>{}(rawId);
						auto ch = [&](int sh) { return 0.40f + 0.45f * (((hs >> sh) & 0xFF) / 255.0f); };
						glm::vec3 base = { ch(0), ch(8), ch(16) };
						glm::vec3 tint = { rc.x, rc.y, rc.z };
						top  = glm::mix(base, tint, 0.35f);
						side = top * 0.78f;
						bot  = top * 0.58f;
					}
					float ix = x + 0.012f;
					float iy = y0 + 0.014f;
					float iw = slotW - 0.024f;
					float ih = h - 0.028f;
					float topRgba[4]  = { top.x,  top.y,  top.z,  0.95f };
					float sideRgba[4] = { side.x, side.y, side.z, 0.95f };
					float botRgba[4]  = { bot.x,  bot.y,  bot.z,  0.95f };
					float band = ih / 3.0f;
					m_rhi->drawRect2D(ix, iy + band * 2, iw, band, topRgba);
					m_rhi->drawRect2D(ix, iy + band,     iw, band, sideRgba);
					m_rhi->drawRect2D(ix, iy,            iw, band, botRgba);
					const float outline[4] = {0.02f, 0.02f, 0.03f, 0.80f};
					m_rhi->drawRect2D(ix, iy,         iw,     0.0015f, outline);
					m_rhi->drawRect2D(ix, iy + ih,    iw,     0.0015f, outline);
					m_rhi->drawRect2D(ix, iy,         0.0015f, ih,     outline);
					m_rhi->drawRect2D(ix + iw, iy,    0.0015f, ih,     outline);
				}

				// Rarity strip along the top edge of the slot — replaces the
				// in-slot name as the "what tier is this" cue.
				const float rarityStrip[4] = { rc.x, rc.y, rc.z, 0.75f };
				m_rhi->drawRect2D(x + 0.004f, y0 + h - 0.004f, slotW - 0.008f, 0.002f, rarityStrip);

				// Count badge — top-right, drawn as a dark chip *over* the
				// swatch so it stays readable regardless of swatch color.
				if (count > 1) {
					char cnt[8]; snprintf(cnt, sizeof(cnt), "x%d", count);
					size_t cntLen = std::strlen(cnt);
					float chipW = 0.010f + 0.011f * (float)cntLen;
					float chipH = 0.022f;
					float chipX = x + slotW - chipW - 0.003f;
					float chipY = y0 + h - chipH - 0.003f;
					const float badge[4] = {0.04f, 0.03f, 0.05f, 0.92f};
					m_rhi->drawRect2D(chipX, chipY, chipW, chipH, badge);
					const float wh[4] = {1.0f, 0.97f, 0.85f, 1.0f};
					m_rhi->drawText2D(cnt, chipX + 0.003f, chipY + 0.004f, 0.62f, wh);
				}
			}

			// Slot key label — bottom-right, dim. Drawn last so it reads
			// cleanly on top of the swatch.
			char lab[4]; std::snprintf(lab, sizeof(lab), "%d", (i + 1) % 10);
			const float keyBright[4] = {0.90f, 0.85f, 0.55f, 0.95f};
			m_rhi->drawText2D(lab, x + slotW - 0.018f, y0 + 0.005f, 0.60f,
				selected ? keyBright : keyDim);
		}

		// ── Hotbar drag/drop overlay ──────────────────────────────────────
		// Transparent ImGui window sized+positioned exactly over the visual
		// hotbar. Each slot is an invisible button that's both a drag source
		// (picks up the current alias) and a drop target (accepts INV_SLOT
		// payloads from hotbar peers OR from the Tab inventory panel).
		// Cursor is only released when inventory is open, so the hotbar is
		// interactive only while Tab is held.
		if (m_invOpen && me && me->inventory) {
			ImGuiIO& io = ImGui::GetIO();
			float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
			// NDC→pixel: x_px = (x_ndc + 1) * 0.5 * w;
			// ImGui y is top-down, hotbar sits near bottom: y_px from top = (1 - y_top_ndc) * 0.5 * h
			float pxStartX = (x0 + 1.0f) * 0.5f * ww;
			float pxStartY = (1.0f - (y0 + h)) * 0.5f * wh;
			float pxSlotW  = slotW * 0.5f * ww;
			float pxSlotH  = h     * 0.5f * wh;
			float pxGap    = gap   * 0.5f * ww;

			ImGui::SetNextWindowPos({pxStartX, pxStartY});
			ImGui::SetNextWindowSize({N * (pxSlotW + pxGap), pxSlotH});
			ImGui::SetNextWindowBgAlpha(0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {pxGap, 0.0f});
			if (ImGui::Begin("##hotbar_dd", nullptr,
				ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoBackground| ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
				// Payload: slot=-1 means "from inventory panel"; slot>=0 means "from hotbar".
				struct DragSlot { char itemId[64]; int slot; };
				for (int i = 0; i < N; i++) {
					if (i > 0) ImGui::SameLine(0.0f, pxGap);
					char bid[24]; snprintf(bid, sizeof(bid), "##hdd%d", i);
					ImGui::InvisibleButton(bid, {pxSlotW, pxSlotH});
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						DragSlot ds{}; ds.slot = i;
						std::snprintf(ds.itemId, sizeof(ds.itemId), "%s", m_hotbar.get(i).c_str());
						ImGui::SetDragDropPayload("INV_SLOT", &ds, sizeof(ds));
						if (ds.itemId[0]) ImGui::Text("%s", ds.itemId);
						else              ImGui::TextDisabled("(empty)");
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
							auto* ds = (const DragSlot*)pl->Data;
							std::string newId = ds->itemId;
							std::string oldId = m_hotbar.get(i);
							// Hotbar↔hotbar drags swap; inventory→hotbar (slot=-1)
							// simply writes newId to slot i, leaving oldId free
							// (it's still in inventory, so repopulate would add
							// it back to a free slot on next S_INVENTORY).
							if (ds->slot >= 0) m_hotbar.set(ds->slot, oldId);
							m_hotbar.set(i, newId);
							// Persist immediately so the arrangement survives
							// process kills / crashes, not just graceful quits.
							m_hotbar.saveToFile(m_hotbarSavePath);
						}
						ImGui::EndDragDropTarget();
					}
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
		}
	}

	// ── Inventory panel (Tab) — same Diablo-style UI as the GL client ───
	// Shared EquipmentUI lives in platform/client/ and is now backend-
	// agnostic (see inventory_visuals.h — GL icon textures guarded by
	// CIVCRAFT_USE_GL_ICONS, VK gets iso-cube fallbacks).
	if (m_invOpen) {
		civcraft::Entity* me = m_server->getEntity(m_server->localPlayerId());
		if (me && me->inventory) {
			// Keep EquipmentUI's internal open flag in sync with m_invOpen
			// so its close-button (X in titlebar) can also flip our state.
			if (!m_equipUI.isOpen()) m_equipUI.toggle();
			m_equipUI.render(*me->inventory, m_server->blockRegistry(),
				(float)m_fbW, (float)m_fbH);
			if (!m_equipUI.isOpen()) m_invOpen = false;
		}
	}

	// ── FPS + position readout (bottom-left) ────────────────────────────
	{
		char buf[128];
		auto* fpsMe = playerEntity();
		glm::vec3 fpsPos = fpsMe ? fpsMe->position : glm::vec3(0);
		std::snprintf(buf, sizeof(buf), "%.0f fps  |  %.0f %.0f %.0f  |  %zu ent  %zu chk",
			ImGui::GetIO().Framerate,
			fpsPos.x, fpsPos.y, fpsPos.z,
			m_server->entityCount(),
			m_chunkMeshes.size());
		const float dim[4] = {0.45f, 0.42f, 0.50f, 0.65f};
		m_rhi->drawText2D(buf, -0.96f, -0.99f, 0.55f, dim);
	}
}

void Game::renderMenu() {
	// Semi-opaque dusk scrim — lets the sky + rotating world preview bleed
	// through behind the menu so the screen isn't a flat black box.
	const float bg[4] = { 0.02f, 0.03f, 0.06f, 0.55f };
	m_rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, bg);

	// Decorative header band
	float pulse = 0.85f + 0.15f * std::sin(m_menuTitleT * 1.8f);
	float gold[4] = { 1.0f * pulse, 0.72f * pulse, 0.25f * pulse, 1.0f };
	m_rhi->drawTitle2D("CIVCRAFT  VULKAN", -0.42f, 0.55f, 2.8f, gold);

	const float tag[4] = {0.85f, 0.80f, 0.70f, 0.90f};
	m_rhi->drawText2D("A Vulkan-native playable slice.", -0.26f, 0.44f, 1.0f, tag);

	// ImGui button panel — centered. We use raw ImGui here because ImGui
	// already ships with both backends and handles hit-testing for free.
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.55f);
	ImGui::SetNextWindowPos(ImVec2(center.x - 160, center.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_Always);
	ImGui::Begin("##menu", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
	ImGui::Spacing();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.18f, 0.28f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.28f, 0.40f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.35f, 0.50f, 1.00f));
	// Also accept ENTER as "press PLAY" for keyboard-only / headless flows.
	bool enterPressed = (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
	                  || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
	if (ImGui::Button("  PLAY  ", ImVec2(280, 52)) || enterPressed) enterPlaying();
	ImGui::Spacing();
	if (ImGui::Button("  HOW TO PLAY  ", ImVec2(280, 38))) {
		// Toggle help overlay via ImGui tooltip in-place.
		ImGui::OpenPopup("help");
	}
	if (ImGui::BeginPopup("help")) {
		ImGui::Text("Move      WASD");
		ImGui::Text("Jump      Space");
		ImGui::Text("Look      Mouse");
		ImGui::Text("Attack    Left Click");
		ImGui::Text("Sprint    Shift");
		ImGui::Text("Menu      Esc");
		if (ImGui::Button("OK", ImVec2(120, 28))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::Spacing();
	if (ImGui::Button("  QUIT  ", ImVec2(280, 38))) m_shouldQuit = true;
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();
	ImGui::End();

	if (!m_lastDeathReason.empty()) {
		const float red[4] = {1.0f, 0.45f, 0.35f, 1.0f};
		m_rhi->drawText2D(m_lastDeathReason.c_str(), -0.25f, -0.55f, 1.0f, red);
	}
}

void Game::renderPaused() {
	// Dim the world behind, then a centered ImGui window with Resume / Menu /
	// Quit. Esc also toggles back to Playing (handled in processInput).
	const float veil[4] = {0.0f, 0.0f, 0.0f, 0.55f};
	m_rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, veil);
	const float gold[4] = {1.0f, 0.82f, 0.35f, 1.0f};
	m_rhi->drawTitle2D("PAUSED", -0.17f, 0.20f, 3.0f, gold);

	ImGuiIO& io = ImGui::GetIO();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.55f);
	ImGui::SetNextWindowPos(ImVec2(center.x - 140, center.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(280, 210), ImGuiCond_Always);
	ImGui::Begin("##paused", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	if (ImGui::Button("  RESUME  ", ImVec2(240, 40))) resumeFromPause();
	ImGui::Spacing();
	static bool showLog = false;
	if (ImGui::Button("  GAME LOG  ", ImVec2(240, 34))) showLog = !showLog;
	ImGui::Spacing();
	if (ImGui::Button("  MAIN MENU  ", ImVec2(240, 34))) enterMenu();
	ImGui::Spacing();
	if (ImGui::Button("  QUIT  ", ImVec2(240, 34))) m_shouldQuit = true;
	ImGui::PopStyleVar();
	ImGui::End();

	if (showLog) {
		auto lines = civcraft::GameLogger::instance().snapshot();
		ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
			ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.03f, 0.05f, 0.95f));
		if (ImGui::Begin("Game Log", &showLog)) {
			for (auto& line : lines) {
				ImVec4 col(0.75f, 0.73f, 0.70f, 1.0f);
				if (line.find("[DECIDE]") != std::string::npos)
					col = ImVec4(0.55f, 0.80f, 0.55f, 1.0f);
				else if (line.find("[COMBAT]") != std::string::npos)
					col = ImVec4(0.90f, 0.40f, 0.35f, 1.0f);
				else if (line.find("[DEATH]") != std::string::npos)
					col = ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
				else if (line.find("[ACTION]") != std::string::npos)
					col = ImVec4(0.55f, 0.65f, 0.90f, 1.0f);
				else if (line.find("[INV]") != std::string::npos)
					col = ImVec4(0.90f, 0.75f, 0.35f, 1.0f);
				ImGui::TextColored(col, "%s", line.c_str());
			}
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}

void Game::renderDeath() {
	const float veil[4] = {0.0f, 0.0f, 0.0f, 0.65f};
	m_rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, veil);
	const float red[4] = { 0.95f, 0.25f, 0.20f, 1.0f };
	m_rhi->drawTitle2D("YOU DIED", -0.25f, 0.15f, 3.0f, red);
	const float hint[4] = { 0.85f, 0.85f, 0.90f, 0.95f };
	m_rhi->drawText2D("Press  R  (or click Respawn)   |   Esc  for menu",
		-0.32f, -0.05f, 1.1f, hint);

	// Clickable Respawn / Menu buttons so the mouse-free flow works without
	// the keyboard. Mirrors the main-menu ImGui window.
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.60f);
	ImGui::SetNextWindowPos(ImVec2(center.x - 140, center.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(280, 120), ImGuiCond_Always);
	ImGui::Begin("##dead", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.25f, 0.25f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.35f, 0.35f, 1.00f));
	if (ImGui::Button("  RESPAWN  ", ImVec2(240, 44))) respawn();
	ImGui::Spacing();
	if (ImGui::Button("  MAIN MENU  ", ImVec2(240, 34))) enterMenu();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();
	ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────
// F3 debug overlay
// ─────────────────────────────────────────────────────────────────────────
void Game::renderDebugOverlay() {
	char buf[256];
	float x = -0.96f, y = 0.90f;
	float step = 0.035f;
	const float dim[4] = {0.85f, 0.85f, 0.90f, 0.95f};

	float fps = ImGui::GetIO().Framerate;
	float fpsCol[4] = {0.85f, 0.85f, 0.90f, 0.95f};
	if (fps < 30) { fpsCol[0] = 1.0f; fpsCol[1] = 0.3f; fpsCol[2] = 0.3f; }
	else if (fps < 45) { fpsCol[0] = 1.0f; fpsCol[1] = 0.8f; fpsCol[2] = 0.2f; }
	snprintf(buf, sizeof(buf), "FPS: %.0f", fps);
	m_rhi->drawText2D(buf, x, y, 0.65f, fpsCol); y -= step;

	civcraft::Entity* me = playerEntity();
	if (me) {
		snprintf(buf, sizeof(buf), "XYZ: %.1f / %.1f / %.1f",
			me->position.x, me->position.y, me->position.z);
		m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

		glm::vec3 srvPos = m_server->getServerPosition(me->id());
		glm::vec3 diff = srvPos - me->position;
		float posErrSq = glm::dot(diff, diff);
		float errCol[4] = {0.5f, 1.0f, 0.5f, 0.85f};
		if (posErrSq > 4.0f) { errCol[0] = 1.0f; errCol[1] = 0.3f; errCol[2] = 0.3f; errCol[3] = 0.95f; }
		snprintf(buf, sizeof(buf), "PosErr2: %.2f", posErrSq);
		m_rhi->drawText2D(buf, x, y, 0.60f, errCol); y -= step;
	}

	glm::vec3 dbgPos = me ? me->position : glm::vec3(0);
	int chkX = (int)std::floor(dbgPos.x) >> 4;
	int chkY = (int)std::floor(dbgPos.y) >> 4;
	int chkZ = (int)std::floor(dbgPos.z) >> 4;
	snprintf(buf, sizeof(buf), "Chunk: %d / %d / %d", chkX, chkY, chkZ);
	m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	snprintf(buf, sizeof(buf), "Entities: %zu  Chunks: %zu",
		m_server->entityCount(), m_chunkMeshes.size());
	m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	const char* modeNames[] = {"FPS", "ThirdPerson", "RPG", "RTS"};
	snprintf(buf, sizeof(buf), "Camera: %s  Admin: %s  Fly: %s",
		modeNames[(int)m_cam.mode],
		m_adminMode ? "ON" : "off",
		m_flyMode   ? "ON" : "off");
	m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	glm::vec3 eye = m_cam.position;
	glm::vec3 dir = m_cam.front();
	auto hit = civcraft::raycastBlocks(m_server->chunks(), eye, dir, 10.0f);
	if (hit) {
		auto& bdef = m_server->blockRegistry()
			.get(m_server->chunks().getBlock(
				hit->blockPos.x, hit->blockPos.y, hit->blockPos.z));
		std::string bname = bdef.display_name.empty() ? bdef.string_id : bdef.display_name;
		snprintf(buf, sizeof(buf), "Block: %s @(%d,%d,%d)",
			bname.c_str(), hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
		m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;
	}

	if (!m_rtsSelect.selected.empty()) {
		snprintf(buf, sizeof(buf), "RTS selected: %zu units", m_rtsSelect.selected.size());
		const float orange[4] = {1.0f, 0.7f, 0.2f, 0.95f};
		m_rhi->drawText2D(buf, x, y, 0.60f, orange); y -= step;
	}

	if (m_cam.mode == civcraft::CameraMode::RTS) {
		size_t activeWaypoints = 0;
		for (const auto& [eid, mo] : m_moveOrders)
			if (mo.active) activeWaypoints++;
		snprintf(buf, sizeof(buf), "Waypoints: %zu active", activeWaypoints);
		const float blue[4] = {0.35f, 0.75f, 1.0f, 0.95f};
		m_rhi->drawText2D(buf, x, y, 0.60f, blue); y -= step;
	}
}

// ─────────────────────────────────────────────────────────────────────────
// Handbook — ImGui artifact browser (H key)
// ─────────────────────────────────────────────────────────────────────────
void Game::renderHandbook() {
	ImGui::PushStyleColor(ImGuiCol_WindowBg,       ImVec4(0.05f, 0.04f, 0.03f, 0.96f));
	ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.40f, 0.30f, 0.12f, 0.70f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg,        ImVec4(0.08f, 0.06f, 0.04f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive,  ImVec4(0.12f, 0.09f, 0.05f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Tab,            ImVec4(0.10f, 0.08f, 0.06f, 0.90f));
	ImGui::PushStyleColor(ImGuiCol_TabSelected,    ImVec4(0.20f, 0.15f, 0.08f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TabHovered,     ImVec4(0.30f, 0.22f, 0.10f, 0.90f));
	ImGui::PushStyleColor(ImGuiCol_Header,         ImVec4(0.15f, 0.12f, 0.06f, 0.70f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.25f, 0.20f, 0.08f, 0.80f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

	ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		ImVec2(m_fbW * 0.5f, m_fbH * 0.5f), ImGuiCond_FirstUseEver,
		ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Handbook", &m_handbookOpen)) {
		auto categories = m_artifactRegistry.allCategories();
		if (ImGui::BeginTabBar("HandbookTabs")) {
			for (auto& cat : categories) {
				auto entries = m_artifactRegistry.byCategory(cat);
				if (entries.empty()) continue;
				std::string label = cat;
				if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
				char tabLabel[64];
				snprintf(tabLabel, sizeof(tabLabel), "%s (%zu)",
					label.c_str(), entries.size());
				if (ImGui::BeginTabItem(tabLabel)) {
					// Left panel: entry list
					ImGui::BeginChild("EntryList", ImVec2(180, 0), true);
					static std::string selectedId;
					for (auto* e : entries) {
						bool sel = (selectedId == e->id);
						ImGui::PushID(e->id.c_str());
						if (ImGui::Selectable(e->name.c_str(), sel))
							selectedId = e->id;
						ImGui::PopID();
					}
					ImGui::EndChild();

					ImGui::SameLine();

					// Right panel: detail
					ImGui::BeginChild("EntryDetail", ImVec2(0, 0), true);
					const civcraft::ArtifactEntry* selected = nullptr;
					for (auto* e : entries)
						if (e->id == selectedId) { selected = e; break; }

					if (selected) {
						ImGui::SetWindowFontScale(1.2f);
						ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f),
							"%s", selected->name.c_str());
						ImGui::SetWindowFontScale(1.0f);
						ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 1.0f),
							"%s", selected->id.c_str());
						ImGui::Separator();

						if (!selected->description.empty()) {
							ImGui::Spacing();
							ImGui::TextWrapped("%s", selected->description.c_str());
							ImGui::Spacing();
						}

						// Properties
						std::vector<std::pair<std::string, std::string>> props;
						float matVal = civcraft::getMaterialValue(selected->id);
						char mvBuf[32]; snprintf(mvBuf, sizeof(mvBuf), "%g", matVal);
						props.push_back({"Material value", mvBuf});
						for (auto& [key, val] : selected->fields) {
							if (key == "name" || key == "id" || key == "description"
							    || key == "subcategory") continue;
							std::string label2 = key;
							for (auto& c : label2) if (c == '_') c = ' ';
							if (!label2.empty()) label2[0] = toupper(label2[0]);
							props.push_back({label2, val});
						}
						if (ImGui::CollapsingHeader("Properties",
						    ImGuiTreeNodeFlags_DefaultOpen)) {
							if (ImGui::BeginTable("Props", 2,
							    ImGuiTableFlags_RowBg |
							    ImGuiTableFlags_BordersInnerH)) {
								ImGui::TableSetupColumn("Property",
									ImGuiTableColumnFlags_WidthFixed, 130);
								ImGui::TableSetupColumn("Value");
								for (auto& [k, v] : props) {
									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::TextColored(
										ImVec4(0.55f, 0.50f, 0.42f, 1.0f),
										"%s", k.c_str());
									ImGui::TableNextColumn();
									ImGui::TextUnformatted(v.c_str());
								}
								ImGui::EndTable();
							}
						}

						// Source code (Python syntax highlighting)
						if (!selected->source.empty() &&
						    ImGui::CollapsingHeader("Source")) {
							ImGui::PushStyleColor(ImGuiCol_ChildBg,
								ImVec4(0.02f, 0.02f, 0.02f, 0.95f));
							ImGui::BeginChild("SourceCode",
								ImVec2(0, 250), true);
							for (auto& line : [&]{
								std::vector<std::string> lines;
								std::istringstream ss(selected->source);
								std::string l;
								while (std::getline(ss, l)) lines.push_back(l);
								return lines;
							}()) {
								std::string_view sv(line);
								auto trimmed = sv;
								while (!trimmed.empty() && trimmed[0] == ' ')
									trimmed.remove_prefix(1);
								ImVec4 color(0.82f, 0.80f, 0.75f, 1.0f);
								if (trimmed.substr(0, 1) == "#")
									color = ImVec4(0.40f, 0.70f, 0.40f, 1.0f);
								else if (trimmed.substr(0, 3) == "def" ||
								         trimmed.substr(0, 5) == "class")
									color = ImVec4(0.45f, 0.55f, 0.90f, 1.0f);
								else if (trimmed.substr(0, 6) == "return" ||
								         trimmed.substr(0, 6) == "import")
									color = ImVec4(0.70f, 0.45f, 0.80f, 1.0f);
								ImGui::TextColored(color, "%s", line.c_str());
							}
							ImGui::EndChild();
							ImGui::PopStyleColor();
						}

						// File path
						if (!selected->filePath.empty()) {
							ImGui::Spacing();
							ImGui::TextColored(
								ImVec4(0.40f, 0.38f, 0.35f, 0.70f),
								"%s", selected->filePath.c_str());
						}
					} else {
						ImGui::TextColored(
							ImVec4(0.50f, 0.48f, 0.42f, 1.0f),
							"Select an entry from the list.");
					}
					ImGui::EndChild();

					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(9);

	if (!m_handbookOpen) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
}

// ─────────────────────────────────────────────────────────────────────────
// Entity inspection overlay
// ─────────────────────────────────────────────────────────────────────────

void Game::renderEntityInspect() {
	civcraft::Entity* e = m_server->getEntity(m_inspectedEntity);
	if (!e) { m_inspectedEntity = 0; return; }

	ImGui::SetNextWindowSize(ImVec2(520, 580), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		ImVec2(m_fbW * 0.5f, m_fbH * 0.5f), ImGuiCond_FirstUseEver,
		ImVec2(0.5f, 0.5f));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.94f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.12f, 0.20f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));

	std::string displayName = e->def().display_name;
	if (displayName.empty()) {
		displayName = e->typeId();
		auto col = displayName.find(':');
		if (col != std::string::npos) displayName = displayName.substr(col + 1);
	}
	for (auto& c : displayName) if (c == '_') c = ' ';
	if (!displayName.empty()) displayName[0] = (char)toupper((unsigned char)displayName[0]);
	std::string title = displayName + " #" + std::to_string(e->id()) + "###inspect";

	bool open = true;
	if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {

		// ── Live stats ────────────────────────────────────────────────
		int curHP = e->hp();
		int maxHP = e->def().max_hp;
		ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "HP: %d / %d", curHP, maxHP);
		if (maxHP > 0) {
			float frac = (float)curHP / (float)maxHP;
			ImVec4 barCol = frac > 0.5f ? ImVec4(0.2f, 0.8f, 0.2f, 1)
			              : frac > 0.25f ? ImVec4(0.9f, 0.7f, 0.1f, 1)
			                             : ImVec4(0.9f, 0.2f, 0.2f, 1);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
			ImGui::ProgressBar(frac, ImVec2(-1, 6));
			ImGui::PopStyleColor();
		}

		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Position: (%.1f, %.1f, %.1f)",
			e->position.x, e->position.y, e->position.z);
		ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1), "Entity ID: %u",
			(unsigned)e->id());
		ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, 1), "Type: %s",
			e->typeId().c_str());

		if (!e->goalText.empty()) {
			ImVec4 goalCol = e->hasError ? ImVec4(1.0f, 0.3f, 0.3f, 1)
			                             : ImVec4(0.5f, 1.0f, 0.8f, 1);
			ImGui::TextColored(goalCol, "Goal: %s", e->goalText.c_str());
		} else {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Goal: (pending)");
		}

		if (e->hasError && !e->errorText.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1));
			ImGui::TextWrapped("Error: %s", e->errorText.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();

		// ── Ownership ─────────────────────────────────────────────────
		int owner = e->getProp<int>(civcraft::Prop::Owner, 0);
		civcraft::EntityId myId = m_server->localPlayerId();
		if (owner == (int)myId)
			ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1), "Owner: you");
		else if (owner != 0)
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Owner: player #%d", owner);
		else
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Owner: world");

		ImGui::Separator();

		// ── Properties ────────────────────────────────────────────────
		if (ImGui::CollapsingHeader("Properties")) {
			const auto& def = e->def();
			if (ImGui::BeginTable("props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140);
				ImGui::TableSetupColumn("Value");
				auto row = [](const char* label, const char* fmt, auto... args) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.80f, 1), "%s", label);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text(fmt, args...);
				};
				row("walk_speed", "%.1f", def.walk_speed);
				row("run_speed", "%.1f", def.run_speed);
				row("max_hp", "%d", def.max_hp);
				std::string bid = e->getProp<std::string>(civcraft::Prop::BehaviorId, "");
				if (!bid.empty()) row("behavior_id", "%s", bid.c_str());
				row("gravity_scale", "%.2f", def.gravity_scale);
				row("collision", "(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)",
					def.collision_box_min.x, def.collision_box_min.y, def.collision_box_min.z,
					def.collision_box_max.x, def.collision_box_max.y, def.collision_box_max.z);
				ImGui::EndTable();
			}
		}

		// ── Inventory ─────────────────────────────────────────────────
		if (e->inventory && !e->inventory->items().empty()) {
			int itemCount = 0;
			for (auto& [iid, cnt] : e->inventory->items()) itemCount += cnt;
			char hdr[64];
			snprintf(hdr, sizeof(hdr), "Inventory (%d items)###inv", itemCount);
			if (ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
				for (auto& [itemId, count] : e->inventory->items()) {
					if (count <= 0) continue;
					std::string name = itemId;
					auto col = name.find(':');
					if (col != std::string::npos) name = name.substr(col + 1);
					for (auto& c : name) if (c == '_') c = ' ';
					if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
					ImGui::BulletText("%s x%d", name.c_str(), count);
				}
			}
		}

		// ── All properties (advanced, collapsed by default) ───────────
		if (ImGui::CollapsingHeader("All Properties (Raw)")) {
			for (auto& [key, val] : e->props()) {
				if (auto* iv = std::get_if<int>(&val))
					ImGui::Text("  %s = %d", key.c_str(), *iv);
				else if (auto* fv = std::get_if<float>(&val))
					ImGui::Text("  %s = %.3f", key.c_str(), *fv);
				else if (auto* sv = std::get_if<std::string>(&val))
					ImGui::Text("  %s = \"%s\"", key.c_str(), sv->c_str());
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(3);
	if (!open) m_inspectedEntity = 0;
}

// ─────────────────────────────────────────────────────────────────────────
// Chest inventory transfer UI
// ─────────────────────────────────────────────────────────────────────────

void Game::renderChestUI() {
	civcraft::Entity* chestE = m_server->getEntity(m_chestUI.chestEid);
	civcraft::Entity* pe = m_server->getEntity(m_server->localPlayerId());
	if (!chestE || !pe) { m_chestUI.open = false; return; }

	ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		ImVec2(m_fbW * 0.5f, m_fbH * 0.5f), ImGuiCond_FirstUseEver,
		ImVec2(0.5f, 0.5f));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.06f, 0.04f, 0.92f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.15f, 0.10f, 0.06f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.25f, 0.15f, 0.08f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.14f, 0.08f, 0.9f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.22f, 0.10f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.30f, 0.12f, 1.0f));

	bool open = m_chestUI.open;
	if (ImGui::Begin("Chest", &open,
	    ImGuiWindowFlags_NoCollapse)) {

		auto renderSection = [&](const char* label, civcraft::Inventory* inv,
		                         bool isChest) {
			ImGui::Text("%s", label);
			ImGui::Separator();
			if (!inv || inv->items().empty()) {
				ImGui::TextDisabled("  (empty)");
			} else {
				for (auto& [itemId, count] : inv->items()) {
					if (count <= 0) continue;
					std::string name = itemId;
					auto col = name.find(':');
					if (col != std::string::npos) name = name.substr(col + 1);
					for (auto& c : name) if (c == '_') c = ' ';
					if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);

					ImGui::Text("  %s x%d", name.c_str(), count);
					ImGui::SameLine();
					std::string btnId = (isChest ? "Take##" : "Store##") + itemId;
					if (ImGui::SmallButton(btnId.c_str())) {
						civcraft::ActionProposal p;
						p.type      = civcraft::ActionProposal::Relocate;
						p.actorId   = m_server->localPlayerId();
						p.itemId    = itemId;
						p.itemCount = 1;
						if (isChest) {
							p.relocateFrom = civcraft::Container::entity(m_chestUI.chestEid);
							p.relocateTo   = civcraft::Container::self();
						} else {
							p.relocateFrom = civcraft::Container::self();
							p.relocateTo   = civcraft::Container::entity(m_chestUI.chestEid);
						}
						m_server->sendAction(p);
						m_server->sendGetInventory(m_chestUI.chestEid);
					}
					ImGui::SameLine();
					std::string allId = (isChest ? "All##" : "All##s") + itemId;
					if (ImGui::SmallButton(allId.c_str())) {
						civcraft::ActionProposal p;
						p.type      = civcraft::ActionProposal::Relocate;
						p.actorId   = m_server->localPlayerId();
						p.itemId    = itemId;
						p.itemCount = count;
						if (isChest) {
							p.relocateFrom = civcraft::Container::entity(m_chestUI.chestEid);
							p.relocateTo   = civcraft::Container::self();
						} else {
							p.relocateFrom = civcraft::Container::self();
							p.relocateTo   = civcraft::Container::entity(m_chestUI.chestEid);
						}
						m_server->sendAction(p);
						m_server->sendGetInventory(m_chestUI.chestEid);
					}
				}
			}
		};

		renderSection("Chest Contents", chestE->inventory.get(), true);
		ImGui::Spacing(); ImGui::Spacing();
		renderSection("Your Inventory", pe->inventory.get(), false);
	}
	ImGui::End();
	ImGui::PopStyleColor(6);
	m_chestUI.open = open;
}

// ─────────────────────────────────────────────────────────────────────────
// RTS box selection rectangle
// ─────────────────────────────────────────────────────────────────────────
void Game::renderRTSSelect() {
	if (m_cam.mode != civcraft::CameraMode::RTS) return;

	// Draw selection highlight rings around selected units
	for (auto eid : m_rtsSelect.selected) {
		civcraft::Entity* e = m_server->getEntity(eid);
		if (!e) continue;
		glm::vec3 ndc;
		if (!projectWorld(e->position + glm::vec3(0, 0.1f, 0), ndc)) continue;
		float r = 0.025f;
		const float selColor[4] = {0.4f, 0.8f, 1.0f, 0.7f};
		m_rhi->drawRect2D(ndc.x - r, ndc.y - r * 0.5f, r * 2, r, selColor);
	}

	// Draw drag rectangle
	if (!m_rtsSelect.dragging) return;
	float x0 = std::min(m_rtsSelect.start.x, m_rtsSelect.end.x);
	float x1 = std::max(m_rtsSelect.start.x, m_rtsSelect.end.x);
	float y0 = std::min(m_rtsSelect.start.y, m_rtsSelect.end.y);
	float y1 = std::max(m_rtsSelect.start.y, m_rtsSelect.end.y);

	// Semi-transparent fill
	const float fill[4] = {0.39f, 0.78f, 1.0f, 0.12f};
	m_rhi->drawRect2D(x0, y0, x1 - x0, y1 - y0, fill);

	// Outline (4 edges)
	float t = 0.003f;
	const float edge[4] = {0.39f, 0.78f, 1.0f, 0.75f};
	m_rhi->drawRect2D(x0, y0, x1 - x0, t, edge);           // top
	m_rhi->drawRect2D(x0, y1 - t, x1 - x0, t, edge);       // bottom
	m_rhi->drawRect2D(x0, y0, t, y1 - y0, edge);            // left
	m_rhi->drawRect2D(x1 - t, y0, t, y1 - y0, edge);        // right
}

} // namespace civcraft::vk

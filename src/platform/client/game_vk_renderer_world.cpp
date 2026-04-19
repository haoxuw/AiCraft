#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <GLFW/glfw3.h>

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
#include "logic/constants.h"
#include "agent/agent_client.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// Rendering — world, entities, effects, HUD
// ─────────────────────────────────────────────────────────────────────────

namespace {

// Six-plane frustum for broad-phase chunk culling. Planes extracted from a
// row-vec VP matrix (Vulkan clip-space: x,y ∈ [-w,w], z ∈ [0,w]). The sign
// convention is "point P is inside if n·P + d ≥ 0" for every plane. The VP
// can include a Y-flip without changing the bounded world volume — we still
// get the same 6-plane envelope, just with top/bottom labels swapped.
struct Frustum {
	glm::vec4 planes[6];
	void setFromVP(const glm::mat4& m) {
		auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
		glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
		planes[0] = r3 + r0;   // left
		planes[1] = r3 - r0;   // right
		planes[2] = r3 + r1;   // bottom
		planes[3] = r3 - r1;   // top
		planes[4] = r2;         // near (Vulkan: z ≥ 0)
		planes[5] = r3 - r2;   // far  (w - z ≥ 0)
	}
	// AABB-vs-frustum: for each plane, test the AABB corner farthest along
	// the plane's outward normal (the "p-vertex"). If that corner is outside,
	// the whole box is outside. Cheap — 6 branches, 6 dot products, no sqrt.
	bool aabbVisible(const glm::vec3& mn, const glm::vec3& mx) const {
		for (const auto& p : planes) {
			glm::vec3 pv(p.x > 0 ? mx.x : mn.x,
			             p.y > 0 ? mx.y : mn.y,
			             p.z > 0 ? mx.z : mn.z);
			if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0) return false;
		}
		return true;
	}
};

// Goal-keyword → animation-clip map. Modders extending goalText don't need
// to touch this file; they add the clip to the model's Python definition.
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

// Frame-rate-independent EMA smoothing. alpha = 1 − exp(−dt/tau); on first
// call the state snaps to target so new entities don't drift in from zero.
// tau is the time constant (seconds) — how long the trail lags real input.
void smoothScalar(float& state, float target, float dt, float tau,
                  bool& initialized) {
	if (!initialized) { state = target; initialized = true; return; }
	if (dt <= 0.0f || tau <= 0.0f) { state = target; return; }
	float alpha = 1.0f - std::exp(-dt / tau);
	state += (target - state) * alpha;
}

// Same, but for degrees — shortest-arc unwrap so 359→1 is treated as +2°.
void smoothAngleDeg(float& state, float target, float dt, float tau,
                    bool& initialized) {
	if (!initialized) { state = target; initialized = true; return; }
	if (dt <= 0.0f || tau <= 0.0f) { state = target; return; }
	float diff = target - state;
	while (diff >  180.0f) diff -= 360.0f;
	while (diff < -180.0f) diff += 360.0f;
	float alpha = 1.0f - std::exp(-dt / tau);
	state += diff * alpha;
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


void WorldRenderer::renderWorld(float wallTime) {
	Game& g = game_;
	// Push the current render-tuning grading to the RHI. Composite shader
	// reads the UBO when the swapchain pass runs, so this needs to land
	// before endFrame. Zeros = clean render; slider values add each FX.
	g.m_rhi->setGrading(g.m_grading);

	// Sun trajectory driven by server worldTime (Rule 3 — server is the sole
	// owner of time-of-day). worldTime ∈ [0,1): 0=midnight, 0.25=dawn,
	// 0.5=noon, 0.75=dusk. Angle convention puts the sun overhead at noon.
	float tod      = g.m_server ? g.m_server->worldTime() : 0.5f;
	// Menu force-lights the backdrop to a bright mid-morning regardless of
	// the live server's time-of-day — the main-menu scene (trees, plaza,
	// character preview) shouldn't be a black silhouette at dawn/dusk.
	if (g.m_state == civcraft::vk::GameState::Menu) {
		tod = 0.42f;  // mid-morning: sun well above horizon, warm slant light
	}
	float sunAngle = tod * 6.2831853f - 1.5707963f;   // 2π·tod − π/2
	glm::vec3 sunDir = glm::normalize(glm::vec3(
		std::cos(sunAngle),
		std::sin(sunAngle),
		0.35f));                                       // slight lateral offset for variety
	// sunStr: 0 at deep night, 1 at full day. Smooth ramp across the horizon.
	float sunStr = glm::smoothstep(-0.10f, 0.22f, sunDir.y);

	// Shadow pass (terrain + entities share the same depth map).
	auto* me = g.playerEntity();
	glm::vec3 pPos = me ? me->position : g.m_cam.position;
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
		if (!g.m_climb.active()) return rawY;
		float u = glm::clamp(g.m_climb.t / g.m_climb.duration, 0.0f, 1.0f);
		u = 1.0f - (1.0f - u) * (1.0f - u);  // ease-out quad
		return g.m_climb.fromY + (g.m_climb.toY - g.m_climb.fromY) * u;
	};

	// Build entity box stream once per frame — shared by shadow + lit passes.
	// Each box is 19 floats: {mat4 model, r, g, b}. Python BoxModel
	// definitions (model_loader::loadAllModels) are flattened through
	// civcraft::appendBoxModel — walk/idle/clip/head-track animation runs here.
	auto& charBoxes = g.m_scratch.charBoxes;
	charBoxes.clear();

	// Model-key resolution — character_skin prop wins; otherwise EntityDef.model
	// (stripped of its .py extension) with deterministic variant selection
	// from the entity id.
	auto resolveModelKey = [&](const civcraft::Entity& e) -> std::string {
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
		int n = civcraft::model_loader::countVariants(g.m_models, key);
		if (n > 0) {
			uint64_t h = (uint64_t)e.id() * 2654435761u;
			return key + "#" + std::to_string((int)(h % (uint64_t)n));
		}
		return key;
	};

	auto resolveItemModel = [&](const std::string& itemId)
	    -> const civcraft::BoxModel* {
		if (itemId.empty()) return nullptr;
		std::string key = itemId;
		auto colon = key.find(':');
		if (colon != std::string::npos) key = key.substr(colon + 1);
		auto it = g.m_models.find(key);
		return (it != g.m_models.end()) ? &it->second : nullptr;
	};

	// Local player body — skip in FPS so the body doesn't eclipse the camera.
	if (me && g.m_cam.mode != civcraft::CameraMode::FirstPerson) {
		auto pit = g.m_models.find(resolveModelKey(*me));
		if (pit != g.m_models.end()) {
			civcraft::AnimState anim{};
			anim.walkDistance = g.m_walkDist;
			{
				// Smooth speed → walk-cycle clip blending doesn't snap on
				// abrupt velocity changes (sprint toggle, wall impact).
				float rawSpeed = glm::length(glm::vec2(me->velocity.x,
				                                       me->velocity.z));
				smoothScalar(g.m_playerAnimSpeed, rawSpeed, g.m_frameDt,
				             /*tau=*/0.15f, g.m_playerAnimSpeedInit);
				anim.speed = g.m_playerAnimSpeed;
			}
			anim.time         = g.m_wallTime;

			// Held items: hotbar selection → main hand, offhand equip → other.
			civcraft::HeldItems held;
			civcraft::HeldItem mainItem;
			mainItem.model = me->inventory
			    ? resolveItemModel(g.m_hotbar.mainHand(*me->inventory))
			    : nullptr;
			civcraft::HeldItem offItem;
			offItem.model = me->inventory
			    ? resolveItemModel(me->inventory->equipped(
			          civcraft::WearSlot::Offhand))
			    : nullptr;
			bool offhandRight = me->inventory
			    && me->inventory->offhandInRightHand();
			if (offhandRight) {
				held.rightHand = offItem; held.leftHand = mainItem;
			} else {
				held.rightHand = mainItem; held.leftHand = offItem;
			}

			glm::vec3 bodyPos(me->position.x, visualPlayerY(me->position.y),
			                  me->position.z);
			civcraft::appendBoxModel(charBoxes, pit->second, bodyPos,
			                         glm::degrees(g.m_playerBodyYaw),
			                         anim, &held);
		}
	}

	// Remote entities — dispatch by EntityKind:
	//   Structure → skip (chunk mesher already owns the geometry)
	//   Item      → Python model if available, height-normalized to ~0.35
	//               blocks; fallback to a colored cube (block-drop case).
	//   Living    → Python model via resolveModelKey + fillMobAnim.
	{
		EntityId myId = g.m_server->localPlayerId();
		auto hasActiveAnim = [&](civcraft::EntityId eid) {
			for (const auto& a : g.m_pickupAnims)
				if (a.itemId == eid) return true;
			return false;
		};
		g.m_server->forEachEntity([&](civcraft::Entity& e) {
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
				float bob    = std::sin(g.m_wallTime * 2.5f + e.id() * 1.7f) * 0.06f;
				float bounce = std::abs(std::sin(g.m_wallTime * 4.0f
				                                 + e.id() * 2.3f)) * 0.04f;
				float ox = ((h & 0xFF) / 255.0f - 0.5f) * 0.3f;
				float oz = (((h >> 8) & 0xFF) / 255.0f - 0.5f) * 0.3f;
				float spinYawDeg = g.m_wallTime * 90.0f + e.id() * 47.0f;

				auto imIt = g.m_models.find(modelKey);
				if (imIt != g.m_models.end()) {
					// Height-normalize to ~0.35 blocks so drops read as pickups.
					civcraft::BoxModel m = imIt->second;
					float mh = std::max(m.totalHeight * m.modelScale, 0.1f);
					float worldScale = 0.35f / mh;
					for (auto& part : m.parts) {
						part.offset *= worldScale;
						part.halfSize *= worldScale;
					}
					civcraft::AnimState anim{};
					anim.time = g.m_wallTime;
					civcraft::appendBoxModel(charBoxes, m,
					    e.position + glm::vec3(ox, bob + bounce + 0.3f, oz),
					    spinYawDeg, anim);
				} else {
					const auto* bdef = g.m_server->blockRegistry().find(itemType);
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
			auto mit = g.m_models.find(mkey);
			if (mit == g.m_models.end()) return;

			civcraft::AnimState anim{};
			float bodyYaw;
			fillMobAnim(e, g.m_wallTime, anim, bodyYaw);

			// EMA-smooth speed + body yaw per entity. Decide() re-aims step
			// velocity in one frame; without this, legs and torso snap.
			auto& sm = g.m_entityAnimSmooth[e.id()];
			smoothScalar   (sm.speed,      anim.speed, g.m_frameDt,
			                /*tau=*/0.15f, sm.initialized);
			bool yawInit = sm.initialized;   // already true after smoothScalar
			smoothAngleDeg (sm.bodyYawDeg, bodyYaw,    g.m_frameDt,
			                /*tau=*/0.12f, yawInit);
			anim.speed = sm.speed;
			bodyYaw    = sm.bodyYawDeg;

			civcraft::appendBoxModel(charBoxes, mit->second,
			                         e.position, bodyYaw, anim);
		});

		// Drop trails for entities that left the world — otherwise the map
		// grows unbounded as mobs respawn with fresh ids.
		for (auto it = g.m_entityAnimSmooth.begin();
		     it != g.m_entityAnimSmooth.end(); ) {
			civcraft::Entity* ent = g.m_server->getEntity(it->first);
			if (!ent || ent->removed) it = g.m_entityAnimSmooth.erase(it);
			else                      ++it;
		}

		// Fly-to-player arc for claimed items — lerp from spawn point to the
		// picker's chest height with smoothstep, shrink as it approaches.
		glm::vec3 target = me ? me->position + glm::vec3(0, 0.8f, 0)
		                      : g.m_cam.position;
		for (const auto& a : g.m_pickupAnims) {
			if (a.duration <= 0.0f) continue;
			float u = glm::clamp(a.t / a.duration, 0.0f, 1.0f);
			float ease = u * u * (3.0f - 2.0f * u);
			glm::vec3 drawPos = glm::mix(a.startPos, target, ease);
			float scale = 1.0f - ease * 0.5f;
			glm::vec3 size(0.3f * scale);
			if (auto* ie = g.m_server->getEntity(a.itemId)) {
				glm::vec3 s = ie->def().collision_box_max
				            - ie->def().collision_box_min;
				size = s * scale;
			}
			civcraft::emitAABox(charBoxes,
			                    drawPos - size * 0.5f, size, a.color);
		}
	}
	// Main-menu backdrop — no chunks stream until HELLO (character-select
	// completes), so the menu camera has nothing but sky to look at. Emit a
	// decorative grass plaza with a few trees and flowers as box instances,
	// on the same pipeline that draws characters. Placed around origin; the
	// menu camera orbits (0, 1.6, 0) so this fills the frame. CharacterSelect
	// reuses the same plaza so the previewed character stands on the grass
	// instead of floating in sky.
	if (g.m_state == civcraft::vk::GameState::Menu) {
		const glm::vec3 grass(0.36f, 0.58f, 0.22f);
		const glm::vec3 grassDk(0.28f, 0.46f, 0.18f);
		const glm::vec3 dirt (0.42f, 0.30f, 0.20f);
		const glm::vec3 stone(0.48f, 0.48f, 0.50f);
		const glm::vec3 bark (0.32f, 0.22f, 0.14f);
		const glm::vec3 leaf (0.22f, 0.44f, 0.18f);
		const glm::vec3 leafLt(0.30f, 0.55f, 0.22f);

		// Ground — 32×32 plaza; grass top, dirt below so the edge shows a cliff.
		for (int z = -16; z < 16; ++z) {
			for (int x = -16; x < 16; ++x) {
				// Gentle checker variation so it's not a flat color.
				const glm::vec3& g0 = ((x + z) & 1) ? grass : grassDk;
				civcraft::emitAABox(charBoxes,
					glm::vec3((float)x, 0.0f, (float)z),
					glm::vec3(1.0f, 1.0f, 1.0f), g0);
				civcraft::emitAABox(charBoxes,
					glm::vec3((float)x, -1.0f, (float)z),
					glm::vec3(1.0f, 1.0f, 1.0f), dirt);
			}
		}

		// Trees — trunk + leaf canopy. Scattered around origin, clear of the
		// camera focus so the orbit frames them.
		auto emitTree = [&](float cx, float cz, float scale) {
			float trunkH = 4.0f * scale;
			civcraft::emitAABox(charBoxes,
				glm::vec3(cx - 0.5f, 1.0f, cz - 0.5f),
				glm::vec3(1.0f, trunkH, 1.0f), bark);
			// Canopy: 5×3×5 leaf cluster, slightly jagged.
			float cyBase = 1.0f + trunkH - 0.5f;
			for (int ly = 0; ly < 3; ++ly) {
				int rad = (ly == 1) ? 2 : 1;
				for (int lz = -rad; lz <= rad; ++lz) {
					for (int lx = -rad; lx <= rad; ++lx) {
						// trim corners on top/bottom layers
						if (ly != 1 && std::abs(lx) + std::abs(lz) > rad) continue;
						const glm::vec3& lc = ((lx + lz + ly) & 1) ? leaf : leafLt;
						civcraft::emitAABox(charBoxes,
							glm::vec3(cx + lx - 0.5f,
							          cyBase + (float)ly,
							          cz + lz - 0.5f),
							glm::vec3(1.0f, 1.0f, 1.0f), lc);
					}
				}
			}
		};
		emitTree(-7.0f, -5.0f, 1.0f);
		emitTree( 6.0f, -6.0f, 1.1f);
		emitTree( 8.0f,  4.0f, 0.9f);
		emitTree(-5.0f,  7.0f, 1.0f);
		emitTree(-9.0f,  1.0f, 0.8f);

		// Scattered flower/stone accents — tiny boxes on the grass.
		const glm::vec3 red(0.90f, 0.28f, 0.20f);
		const glm::vec3 yel(0.95f, 0.85f, 0.25f);
		const glm::vec3 blu(0.30f, 0.45f, 0.90f);
		struct Accent { float x, z; glm::vec3 c; };
		const Accent accents[] = {
			{ 2.5f,  1.5f, red}, { 3.5f,  2.5f, yel}, { -2.5f, 3.5f, blu},
			{-3.5f, -2.5f, yel}, { 1.0f, -3.5f, red}, {  4.0f,-2.5f, blu},
			{-4.0f,  0.5f, yel}, {-1.5f,  4.5f, red},
		};
		for (const auto& a : accents) {
			civcraft::emitAABox(charBoxes,
				glm::vec3(a.x - 0.15f, 1.0f, a.z - 0.15f),
				glm::vec3(0.30f, 0.40f, 0.30f), a.c);
		}
		// A small rock cluster — visual interest in the open.
		civcraft::emitAABox(charBoxes, glm::vec3(2.5f, 1.0f, -6.5f),
			glm::vec3(1.3f, 0.6f, 1.0f), stone);
		civcraft::emitAABox(charBoxes, glm::vec3(3.0f, 1.5f, -6.0f),
			glm::vec3(0.8f, 0.5f, 0.7f), stone);
	}

	// CharacterSelect / Connecting preview — inject the hovered playable at a
	// fixed world pose so the camera (pinned in game_vk.cpp menu block) frames
	// it to the right of the menu panel. Model rotates slowly so the player
	// can read all sides without having to drag the camera.
	if (g.m_state == civcraft::vk::GameState::Menu
	    && (g.m_menuScreen == civcraft::vk::MenuScreen::CharacterSelect
	        || g.m_menuScreen == civcraft::vk::MenuScreen::Connecting)
	    && !g.m_previewCreatureId.empty()) {
		const civcraft::ArtifactEntry* entry =
		    g.m_artifactRegistry.findById(g.m_previewCreatureId);
		if (entry) {
			std::string key;
			auto mit = entry->fields.find("model");
			if (mit != entry->fields.end() && !mit->second.empty()) key = mit->second;
			else key = entry->id;
			auto dot = key.rfind('.');
			if (dot != std::string::npos) key = key.substr(0, dot);
			auto it = g.m_models.find(key);
			if (it != g.m_models.end()) {
				civcraft::AnimState anim{};
				anim.time = g.m_wallTime;
				anim.currentClip = "mine";
				float spinYaw = g.m_wallTime * 30.0f;  // slow turntable
				// Stands on the plaza grass (y=1, top of the ground slab).
				// The pinned camera in game_vk.cpp positions the viewport so
				// this world pose lands in the right half of the screen.
				civcraft::appendBoxModel(charBoxes, it->second,
				                         glm::vec3(0.0f, 1.0f, 0.0f),
				                         spinYaw, anim);
			}
		}
	}

	uint32_t charBoxCount = (uint32_t)(charBoxes.size() / 19);

	// Shadow pass: every in-range chunk casts via the chunk-mesh shadow
	// pipeline, then characters via the box-shadow pipeline. All three
	// pipelines (voxel/box/chunk) accumulate into the same depth map. Skip
	// at night — a sun below the horizon projects shadows from underground.
	if (sunStr > 0.05f) {
		Frustum shadowFr;
		shadowFr.setFromVP(shadowVP);
		const float CS = (float)civcraft::CHUNK_SIZE;
		for (const auto& kv : g.m_chunkMeshes) {
			if (kv.second == rhi::IRhi::kInvalidMesh) continue;
			glm::vec3 mn(kv.first.x * CS, kv.first.y * CS, kv.first.z * CS);
			glm::vec3 mx = mn + glm::vec3(CS);
			if (!shadowFr.aabbVisible(mn, mx)) continue;
			g.m_rhi->renderShadowsChunkMesh(&shadowVP[0][0], kv.second);
		}
		g.m_rhi->renderBoxShadows(&shadowVP[0][0], charBoxes.data(), charBoxCount);
	}

	// Sky — sun direction, strength, and timeSec drive the procedural shader
	// (LUT-driven zenith/horizon, sunrise bleed, stars + moon at night).
	glm::mat4 vp = g.viewProj();
	glm::mat4 invVP = glm::inverse(vp);
	// Pass worldTime (in "day units") as the shader's animated phase — drives
	// star twinkle and cloud drift. Using server time (not wallTime) keeps
	// cloud motion consistent across all connected clients.
	float skyTime = tod * 24.0f;  // hours since midnight, purely for animation phase
	g.m_rhi->drawSky(&invVP[0][0], &sunDir.x, sunStr, skyTime);

	// Terrain — one drawChunkMeshOpaque per loaded chunk. The mesher
	// already trimmed hidden faces / applied AO + per-face shade, so this
	// is dramatically less geometry than the old per-voxel instancing.
	rhi::IRhi::SceneParams scene{};
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = g.m_cam.position;
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;
	scene.sunDir[0] = sunDir.x; scene.sunDir[1] = sunDir.y; scene.sunDir[2] = sunDir.z;
	scene.sunStr = sunStr;

	// Season/weather color grading now lives in the composite RenderTuning
	// UBO (see GradingParams). The terrain shader no longer carries season
	// or rain — all global color filters are panel-tunable in one place.
	// Fog tracks the sky's horizon color so distant geometry dissolves into
	// the actual horizon tint, not a mismatched cold blue. sunStr drives the
	// warm→cool blend (dawn/dusk pushes toward peach; overcast→deep blue).
	// A distinct "deep-night" palette kicks in below sunStr≈0 so distant
	// geometry reads as black/indigo, matching the starfield overhead.
	glm::vec3 fogNight{0.025f, 0.035f, 0.082f};  // matches horizonNight
	glm::vec3 fogDawn {0.920f, 0.490f, 0.320f};  // matches horizonDawn (warm peach)
	glm::vec3 fogDay  {0.482f, 0.643f, 1.000f};  // matches Mojang canonical sky #7BA4FF
	float dayBlend  = glm::smoothstep(0.15f, 0.70f, sunStr);
	float dawnBlend = glm::smoothstep(0.00f, 0.35f, sunStr);
	glm::vec3 fogMix = glm::mix(glm::mix(fogNight, fogDawn, dawnBlend), fogDay, dayBlend);

	// Weather override: rain / snow desaturate toward a flat overcast palette
	// AND pull the fog in so distant terrain dissolves into the storm. leaves
	// is purely decorative — no fog change.
	float fogNear = 140.0f, fogFar = 320.0f;
	if (g.m_server) {
		const std::string& wk = g.m_server->weatherKind();
		float wi = glm::clamp(g.m_server->weatherIntensity(), 0.0f, 1.0f);
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
	// Frustum cull + distance cull — fog fades geometry to invisible past
	// fogFar, so chunks outside the view cone or beyond (fogFar + diag) can
	// be skipped without any visible change. With render radius 12 this
	// typically drops draw calls from ~2000 to ~200-400.
	Frustum viewFr;
	viewFr.setFromVP(vp);
	const float CS = (float)civcraft::CHUNK_SIZE;
	const float kCullDistSq = (fogFar + CS * 2.0f) * (fogFar + CS * 2.0f);
	glm::vec3 camPos = g.m_cam.position;
	for (const auto& kv : g.m_chunkMeshes) {
		if (kv.second == rhi::IRhi::kInvalidMesh) continue;
		glm::vec3 mn(kv.first.x * CS, kv.first.y * CS, kv.first.z * CS);
		glm::vec3 mx = mn + glm::vec3(CS);
		// Distance: use closest AABB corner to camera.
		glm::vec3 clamped = glm::clamp(camPos, mn, mx);
		glm::vec3 delta = clamped - camPos;
		if (glm::dot(delta, delta) > kCullDistSq) continue;
		if (!viewFr.aabbVisible(mn, mx)) continue;
		g.m_rhi->drawChunkMeshOpaque(scene, fogColor, fogNear, fogFar, kv.second);
	}

	// Entities
	g.m_rhi->drawBoxModel(scene, charBoxes.data(), charBoxCount);

	// ── Block highlight — wireframe outline on targeted block ───────────
	// Raycast from camera and draw 12 thin dark boxes for the cube edges
	// as a dual-pass outline.
	{
		glm::vec3 rayEye = g.m_cam.position;
		glm::vec3 rayDir = g.m_cam.front();
		if (g.m_cam.mode == civcraft::CameraMode::RPG ||
		    g.m_cam.mode == civcraft::CameraMode::RTS) {
			double mx, my;
			glfwGetCursorPos(g.m_window, &mx, &my);
			int ww = g.m_fbW, wh = g.m_fbH;
			if (ww > 0 && wh > 0) {
				float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
				float ndcY = 1.0f - (float)(my / wh) * 2.0f;
				glm::mat4 invVPhl = glm::inverse(vp);
				glm::vec4 nearW = invVPhl * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nearW /= nearW.w;
				glm::vec4 farW  = invVPhl * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); farW  /= farW.w;
				rayDir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));
			}
		}
		auto hlHit = civcraft::raycastBlocks(g.m_server->chunks(), rayEye, rayDir, 6.0f);
		if (hlHit && !g.m_uiWantsCursor) {
			glm::vec3 bp = glm::vec3(hlHit->blockPos);
			float eh = 0.005f;    // edge half-thickness
			float in = -0.002f;   // inset to avoid z-fighting
			glm::vec3 col(0.15f, 0.15f, 0.15f);
			glm::vec3 len(1.0f - 2*in, eh * 2, eh * 2);   // X-axis edge
			glm::vec3 tallY(eh * 2, 1.0f - 2*in, eh * 2); // Y-axis edge
			glm::vec3 tallZ(eh * 2, eh * 2, 1.0f - 2*in); // Z-axis edge
			auto& hl = g.m_scratch.hlBoxes;
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
			g.m_rhi->drawBoxModel(scene, hl.data(),
			                    (uint32_t)(hl.size() / 19));
		}
	}

	// Door swing animations — hinged-panel sweep over the chunk mesh pipeline.
	if (!g.m_doorAnims.empty()) {
		constexpr float kDuration = 0.25f;
		auto& dverts = g.m_scratch.doorVerts;
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
		for (const auto& a : g.m_doorAnims) {
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
			if (g.m_doorAnimMesh == rhi::IRhi::kInvalidMesh)
				g.m_doorAnimMesh = g.m_rhi->createChunkMesh(dverts.data(), vc);
			else
				g.m_rhi->updateChunkMesh(g.m_doorAnimMesh, dverts.data(), vc);
			if (g.m_doorAnimMesh != rhi::IRhi::kInvalidMesh)
				g.m_rhi->drawChunkMeshOpaque(scene, fogColor, 140.0f, 320.0f, g.m_doorAnimMesh);
		}
	}
}

void WorldRenderer::renderEntities(float /*wallTime*/) {
	Game& g = game_;
	// Box-model rendering already happens inside renderWorld so terrain +
	// entities share scene params with no extra state bookkeeping.
}

void WorldRenderer::renderEffects(float wallTime) {
	Game& g = game_;
	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = g.viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float)*16);
	glm::vec3 eye = g.m_cam.position;
	scene.camPos[0] = eye.x; scene.camPos[1] = eye.y; scene.camPos[2] = eye.z;
	scene.time = wallTime;

	// Particle buffer. Weather (rain/snow/leaves) and entity-owned FX (e.g.
	// Monument flame ribbon) push into this below. The world has NO ambient
	// flames, fireflies, or dust motes — those belong to specific entities
	// that emit them, not to the scene at large.
	auto& particles = g.m_scratch.particles;
	particles.clear();
	auto pushP = [&](glm::vec3 p, float size, glm::vec3 rgb, float a) {
		particles.push_back(p.x); particles.push_back(p.y); particles.push_back(p.z);
		particles.push_back(size);
		particles.push_back(rgb.x); particles.push_back(rgb.y); particles.push_back(rgb.z);
		particles.push_back(a);
	};
	// ── Weather particles ─────────────────────────────────────────────
	// Rain streaks / snowflakes / drifting leaves, sampled in a cylinder
	// around the camera so they move with the player and only live near
	// the view frustum. Server-broadcast weather kind + intensity drive
	// count and palette; wind vector tilts the fall direction.
	if (g.m_server) {
		const std::string& wkind = g.m_server->weatherKind();
		float wi = glm::clamp(g.m_server->weatherIntensity(), 0.0f, 1.0f);
		glm::vec2 wind = g.m_server->weatherWind();
		glm::vec3 eyeP = g.m_cam.position;

		// Collision: skip particles whose world cell is a solid block. The
		// ChunkSource lookup is cheap (hash into loaded chunks) and prevents
		// raindrops from streaking through the floor at the cost of one
		// getBlock() call per particle.
		civcraft::ChunkSource& cs = g.m_server->chunks();
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

	// ── Monument flame FX ─────────────────────────────────────────────
	// For every Monument structure entity, wrap a ribbon of rising warm
	// particles around the tower column. Position comes from the entity so
	// modders can place monuments anywhere; height could scale from the
	// GrowthStage property once the server-side growth loop lands.
	if (g.m_server) {
		glm::vec3 eyeP = g.m_cam.position;
		g.m_server->forEachEntity([&](civcraft::Entity& e) {
			if (e.typeId() != civcraft::StructureName::Monument) return;
			glm::vec3 anchor = e.position;
			// Cull by distance — the render radius matches view range so we
			// don't emit 200 particles per frame for a monument five chunks
			// away that's fog-clipped anyway.
			glm::vec3 d = anchor - eyeP;
			float dist2 = d.x*d.x + d.y*d.y + d.z*d.z;
			if (dist2 > 240.0f * 240.0f) return;

			int stage = e.getProp<int>(civcraft::Prop::GrowthStage);
			if (stage <= 0) stage = 18;
			// Flame drifts from the deck up past the trident crown. Kept
			// sparse — subtle glow, not a pyre.
			float colBottom = anchor.y - 1.0f;
			float colTop    = anchor.y + 10.0f;
			float span      = colTop - colBottom;
			float orbitR    = 3.2f;
			constexpr int kPerRib = 14;
			for (int k = 0; k < kPerRib; k++) {
				float seed  = (float)k;
				float phase = std::fmod(seed * 0.091f, 1.0f);
				float t     = std::fmod(wallTime * 0.14f + phase, 1.0f);
				float y     = colBottom + t * span;
				// Slow breathing radius — one gentle orbit, no second ribbon.
				float noise = std::sin(wallTime * 0.9f + seed * 2.7f) * 0.35f;
				float r     = orbitR + noise;
				float ang   = wallTime * 0.45f + seed * 0.42f;
				// Warm but muted. Additive blend values stay close to 1.0 so
				// the flame reads as a subtle glow, not a torch.
				float climb = t;
				glm::vec3 col = glm::mix(
					glm::vec3(1.4f, 0.70f, 0.18f),   // ember orange at base
					glm::vec3(1.1f, 1.05f, 0.45f),   // soft amber at tip
					climb);
				float edgeFade = std::min(t * 4.0f, std::min(1.0f, (1.0f - t) * 4.0f));
				float alpha    = 0.35f * edgeFade;
				glm::vec3 p = anchor + glm::vec3(
					std::cos(ang) * r,
					y - anchor.y,
					std::sin(ang) * r);
				pushP(p, 0.18f, col, alpha);
			}
		});
	}

	uint32_t particleCount = (uint32_t)(particles.size() / 8);
	if (particleCount > 0) g.m_rhi->drawParticles(scene, particles.data(), particleCount);

	// ── Sword slash ribbons (one per active swing) ──────────────────────
	for (const auto& s : g.m_slashes) {
		float swingT = s.t / s.duration;    // 0..1
		// Arc from upper-right → forward → lower-left, relative to dir.
		constexpr int N = 16;
		auto& rbuf = g.m_scratch.ribbons;
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
		if (rc >= 2) g.m_rhi->drawRibbon(scene, rbuf.data(), rc);
	}

	// ── Block break progress overlay ────────────────────────────────────
	// Dark crack-mark particles on each face of the block, progressively
	// denser through 3 stages.
	if (g.m_breaking.active && g.m_breaking.hits > 0) {
		float progress = (float)g.m_breaking.hits / 3.0f;
		glm::vec3 bpf = glm::vec3(g.m_breaking.target);

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

		auto& crackParts = g.m_scratch.crackParts;
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
		g.m_rhi->drawParticles(scene, crackParts.data(), (uint32_t)total);
	}

	// ── Mining hit event particles (burst per swing) ────────────────────
	for (const auto& he : g.m_hitEvents) {
		float age = he.t / 0.4f;
		if (age > 1.0f) continue;
		int n = 8;
		auto& hitParts = g.m_scratch.hitParts;
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
		g.m_rhi->drawParticles(scene, hitParts.data(), (uint32_t)n);
	}

	// ── Move target marker: spinning triangle hovering above the target.
	// SC2/MOBA-style waypoint — 3 bright vertices forming an equilateral
	// triangle, rotating around Y, with faint edge particles tracing the
	// outline. Plus a pulsing down-arrow pointing at the clicked block.
	auto emitGoTriangle = [&](glm::vec3 target, glm::vec3 col, float scale) {
		glm::vec3 center = target + glm::vec3(0, 1.4f * scale, 0);
		float spin = wallTime * 2.5f;
		float r = 0.55f * scale;
		auto& spinParts = g.m_scratch.spinParts;
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

	if (g.m_hasMoveOrder) {
		auto& spinParts = g.m_scratch.spinParts;
		spinParts.clear();
		emitGoTriangle(g.m_moveOrderTarget, glm::vec3(0.30f, 1.0f, 0.45f), 1.0f);
		g.m_rhi->drawParticles(scene, spinParts.data(),
			(uint32_t)(spinParts.size() / 8));
	}

	// RTS selection head markers — rotating yellow triangle hovering above
	// each selected unit's head. Reads as "this unit is under my command"
	// from any camera angle, unlike the small ground ring which gets
	// occluded by terrain and squashed from overhead.
	if (g.m_cam.mode == civcraft::CameraMode::RTS && !g.m_rtsSelect.selected.empty()) {
		auto& spinParts = g.m_scratch.spinParts;
		spinParts.clear();
		const glm::vec3 selCol(1.0f, 0.95f, 0.30f);
		float spin = wallTime * 2.5f;
		for (auto eid : g.m_rtsSelect.selected) {
			civcraft::Entity* e = g.m_server->getEntity(eid);
			if (!e) continue;
			float topY = e->position.y + e->def().collision_box_max.y + 0.7f;
			glm::vec3 c(e->position.x, topY, e->position.z);
			// 3 bright corner particles forming a spinning triangle around
			// the head.
			glm::vec3 verts[3];
			for (int i = 0; i < 3; i++) {
				float a = spin + (float)i * ((float)M_PI * 2.0f / 3.0f);
				verts[i] = c + glm::vec3(std::cos(a) * 0.30f, 0, std::sin(a) * 0.30f);
				spinParts.push_back(verts[i].x);
				spinParts.push_back(verts[i].y);
				spinParts.push_back(verts[i].z);
				spinParts.push_back(0.08f);
				spinParts.push_back(selCol.x);
				spinParts.push_back(selCol.y);
				spinParts.push_back(selCol.z);
				spinParts.push_back(1.0f);
			}
			// Downward tip: a small dot just above the unit's head pointing
			// at it, so selection reads even at low angles.
			glm::vec3 tip = c - glm::vec3(0, 0.30f, 0);
			spinParts.push_back(tip.x);
			spinParts.push_back(tip.y);
			spinParts.push_back(tip.z);
			spinParts.push_back(0.09f);
			spinParts.push_back(selCol.x);
			spinParts.push_back(selCol.y);
			spinParts.push_back(selCol.z);
			spinParts.push_back(0.9f);
		}
		if (!spinParts.empty())
			g.m_rhi->drawParticles(scene, spinParts.data(),
				(uint32_t)(spinParts.size() / 8));
	}

	// Waypoint / plan visualization.
	//
	//   Always-on in RTS:  red/blue dashes along the flow field for each
	//                      unit currently holding a move order (g.m_moveOrders).
	//   F3 in any mode:    green dashes for every entity with a known plan —
	//                      RTS-commanded (via traceFlow) OR running a Python
	//                      agent behavior (via AgentClient::PlanViz). Agents
	//                      already drawn by the RTS pass are skipped to avoid
	//                      double lines.
	{
		auto& spinParts = g.m_scratch.spinParts;
		spinParts.clear();
		constexpr float kStep    = 0.40f;
		constexpr float kDotSize = 0.14f;
		constexpr float kYLift   = 0.25f;
		const float phase = std::fmod(wallTime * 1.5f, kStep * 2.0f);

		// Lay down alternating A/B dots along a polyline, scrolling toward
		// the end. `dotIdxSeed` offsets the stripe pattern so different
		// paths don't all start on the same color.
		auto emitDashes = [&](const std::vector<glm::vec3>& path,
		                      const glm::vec3& colA, const glm::vec3& colB,
		                      int dotIdxSeed) {
			if (path.size() < 2) return;
			float accum = -phase;
			int dotIdx  = dotIdxSeed;
			for (size_t i = 0; i + 1 < path.size(); i++) {
				glm::vec3 a = path[i];
				glm::vec3 b = path[i + 1];
				float segLen = glm::length(b - a);
				if (segLen < 0.001f) continue;
				glm::vec3 dir = (b - a) / segLen;
				float t = accum;
				while (t < segLen) {
					if (t >= 0) {
						glm::vec3 p = a + dir * t;
						const glm::vec3& c = (dotIdx & 1) ? colB : colA;
						spinParts.push_back(p.x); spinParts.push_back(p.y); spinParts.push_back(p.z);
						spinParts.push_back(kDotSize);
						spinParts.push_back(c.x); spinParts.push_back(c.y); spinParts.push_back(c.z);
						spinParts.push_back(0.90f);
					}
					t += kStep;
					dotIdx++;
				}
				accum = t - segLen;
			}
		};

		// Build path: unit → flow trace cells → formation slot. Returns the
		// destination point for the triangle marker. If no flow field is up
		// yet, degrades to a two-point line to the raw target.
		auto buildRtsPath = [&](civcraft::Entity& e, glm::vec3 fallback,
		                        std::vector<glm::vec3>& path) -> glm::vec3 {
			path.push_back(e.position + glm::vec3(0, kYLift, 0));
			glm::vec3 dest = fallback;
			if (g.m_rtsExec.field()) {
				glm::ivec3 cell{
					(int)std::floor(e.position.x),
					(int)std::floor(e.position.y),
					(int)std::floor(e.position.z)};
				auto trace = g.m_rtsExec.traceFlow(cell, 64);
				for (auto& p : trace)
					path.push_back(p + glm::vec3(0, kYLift, 0));
				auto slot = g.m_rtsExec.formationSlot(e.id());
				if (slot)
					dest = glm::vec3{slot->x + 0.5f, (float)slot->y + 0.3f, slot->z + 0.5f};
				else if (!trace.empty())
					dest = trace.back();
			}
			if (path.size() < 2)
				path.push_back(dest + glm::vec3(0, kYLift, 0));
			return dest;
		};

		// Always-on RTS move orders (red/blue).
		std::unordered_set<civcraft::EntityId> rtsDrawn;
		if (g.m_cam.mode == civcraft::CameraMode::RTS && !g.m_moveOrders.empty()) {
			const glm::vec3 colA(1.00f, 0.15f, 0.15f);
			const glm::vec3 colB(0.20f, 0.45f, 1.00f);
			for (const auto& [eid, mo] : g.m_moveOrders) {
				if (!mo.active) continue;
				civcraft::Entity* e = g.m_server->getEntity(eid);
				if (!e) continue;
				std::vector<glm::vec3> path;
				path.reserve(16);
				glm::vec3 dest = buildRtsPath(*e, mo.target, path);
				emitGoTriangle(dest, colA, 0.9f);
				emitDashes(path, colA, colB, (int)eid);
				rtsDrawn.insert(eid);
			}
		}

		// F3: universal plan viz across all camera modes.
		if (g.m_showDebug) {
			const glm::vec3 rtsColA(0.30f, 1.00f, 0.50f);   // bright green
			const glm::vec3 rtsColB(0.10f, 0.65f, 0.25f);   // deep green
			const glm::vec3 agentColA(0.70f, 1.00f, 0.40f); // yellow-green
			const glm::vec3 agentColB(0.25f, 0.55f, 0.15f); // olive

			// Every RTS-commanded entity not already drawn above.
			if (g.m_rtsExec.field()) {
				g.m_server->forEachEntity([&](civcraft::Entity& e) {
					if (!g.m_rtsExec.has(e.id())) return;
					if (rtsDrawn.count(e.id())) return;
					std::vector<glm::vec3> path;
					path.reserve(16);
					glm::vec3 dest = buildRtsPath(e, e.position, path);
					emitGoTriangle(dest, rtsColA, 0.6f);
					emitDashes(path, rtsColA, rtsColB, (int)e.id());
					rtsDrawn.insert(e.id());
				});
			}

			// Every autonomous agent with Python waypoints.
			if (g.m_agentClient) {
				g.m_agentClient->forEachAgent(
					[&](civcraft::EntityId eid,
					    const civcraft::PlanViz& viz) {
					if (viz.waypoints.empty()) return;
					if (rtsDrawn.count(eid)) return;
					civcraft::Entity* a = g.m_server->getEntity(eid);
					if (!a) return;
					std::vector<glm::vec3> path;
					path.reserve(viz.waypoints.size() + 1);
					path.push_back(a->position + glm::vec3(0, kYLift, 0));
					for (auto& wp : viz.waypoints)
						path.push_back(wp + glm::vec3(0, kYLift, 0));
					emitGoTriangle(viz.waypoints.back(), agentColA, 0.6f);
					emitDashes(path, agentColA, agentColB, (int)eid);
				});
			}
		}

		if (!spinParts.empty())
			g.m_rhi->drawParticles(scene, spinParts.data(),
				(uint32_t)(spinParts.size() / 8));
	}

	// FPS viewmodel (hand cube) removed — read as a floating black blob
	// in first-person, not a hand. Bring back as a proper articulated
	// box-model if/when needed.
}

} // namespace civcraft::vk

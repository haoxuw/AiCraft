#include "client/game.h"
#include "shared/constants.h"
#include "shared/physics.h"
#include "server/server_tuning.h"
#include "imgui.h"
#include <sstream>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>

namespace civcraft {

// ============================================================
// updatePlaying helpers
// ============================================================

void Game::takeControlOf(EntityId eid) {
	if (!m_server || !m_server->isConnected()) return;
	EntityId me  = m_server->localPlayerId();
	EntityId cur = m_server->controlledEntityId();
	if (cur == eid) return;

	Entity* target = m_server->getEntity(eid);
	if (!target) return;

	// Leaving an NPC: resume its AI.
	if (cur != me && m_agentClient) m_agentClient->resumeAgent(cur);

	m_server->setControlledEntityId(eid);

	// Entering an NPC: pause its AI so it stops wandering while driven.
	if (eid != me && m_agentClient) m_agentClient->pauseAgent(eid);

	// Rebind the client-only hotbar to the new controlled inventory
	// (may be empty for a pig, etc. — that's fine, it just reads empty slots).
	if (target->inventory) m_hotbar.repopulateFrom(*target->inventory);

	// Snap camera smoothing so the view doesn't sweep across the world to
	// catch up to the new body.
	m_camera.player.feetPos = target->position;
	m_camera.player.yaw     = target->yaw;
	m_camera.rtsCenter      = target->position;
	m_camera.resetSmoothing();

	printf("[Control] Now driving entity %u (%s)\n",
		eid, target->typeId().c_str());
}

// Returns true if the connection was lost and handled (caller should return).
// Hands off to the DISCONNECTED modal; the user chooses Reconnect or Menu.
bool Game::handleConnectionReconnect(float dt) {
	auto drop = [&](const char* fallback) {
		std::string reason = (m_server && !m_server->lastError().empty())
			? m_server->lastError()
			: fallback;
		enterDisconnected(reason.c_str());
	};

	if (!m_server || !m_server->isConnected()) {
		drop("connection lost");
		return true;
	}

	m_server->tick(dt);

	if (!m_server->isConnected()) {
		drop("connection lost");
		return true;
	}

	return false;
}

void Game::handleGameplayInput(float dt) {
	Entity* pe = playerEntity();
	if (!pe) return;

	// [ / ] cycle through owned Living entities (FPS/TPS/RPG). Matches the
	// Control button in the entity inspector: find all Living entities owned
	// by the local player (plus the player itself), sort by id, step ±1 from
	// the currently controlled entity, and hand off via takeControlOf().
	if (m_camera.mode != CameraMode::RTS) {
		bool wantPrev = m_controls.pressed(Action::ControlPrev);
		bool wantNext = m_controls.pressed(Action::ControlNext);
		if (wantPrev || wantNext) {
			EntityId me  = m_server->localPlayerId();
			EntityId cur = m_server->controlledEntityId();
			std::vector<EntityId> owned;
			m_server->forEachEntity([&](Entity& e) {
				if (!e.def().isLiving()) return;
				if (e.id() == me) { owned.push_back(e.id()); return; }
				int owner = e.getProp<int>(Prop::Owner, 0);
				if (owner == (int)me) owned.push_back(e.id());
			});
			if (!owned.empty()) {
				std::sort(owned.begin(), owned.end());
				auto it = std::find(owned.begin(), owned.end(), cur);
				int idx = (it != owned.end()) ? (int)(it - owned.begin()) : 0;
				int n = (int)owned.size();
				int next = wantNext ? (idx + 1) % n : (idx - 1 + n) % n;
				takeControlOf(owned[next]);
			}
		}
	}

	// Tell gameplay if UI wants the cursor (inventory, ImGui, chest, etc.)
	m_gameplay.setUIWantsCursor(m_equipUI.isOpen() || m_chestUI.isOpen() || m_ui.wantsMouse());

	// Client-side: gather input → ActionProposals (works for local AND network)
	float jumpVel = 10.5f; // tuned for gravity=32: reaches ~1.7 blocks
	m_gameplay.update(dt, m_state, *m_server, *pe, m_hotbar, m_camera, m_controls,
	                  m_renderer, m_particles, m_window, jumpVel);

	// Register built-in attack clips once
	AttackAnimPlayer::registerBuiltins();

	// Block-break: short single-clip swing (does not affect combo state)
	if (m_gameplay.swingTriggered()) {
		m_attackAnim.triggerOnce("swing_right");
		m_gameplay.clearSwing();
	}
	// Tier 2b: hit-stop freezes the swing animation briefly on impact.
	m_attackAnim.update(dt * m_combatFx.attackDtScale());
	// All combat FX (shockwave, blade trail, hit-stop, …) live in CombatFxController.
	m_combatFx.update(dt, m_attackAnim, m_particles, *pe);
	if (!m_attackAnim.active() && !m_gameplay.isBreaking())
		m_lastAttackTargetId = ENTITY_NONE;

	// ── Entity attack: swing on left-click + optionally send ActionProposal ──
	m_attackCD -= dt;
	{
		EntityId attackId = m_gameplay.attackTarget();
		if (attackId != ENTITY_NONE)
			m_lastAttackTargetId = attackId;
		m_gameplay.clearAttack();

		if (pe->inventory) {
			int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
			const std::string& itemId = m_hotbar.get(slot);

			// Reload combo when the held item changes
			if (itemId != m_comboItemId) {
				std::vector<std::string> combo = {"swing_left", "swing_right", "cleave"};
				if (!itemId.empty()) {
					const ArtifactEntry* art = m_artifacts.findById(itemId);
					if (art) {
						auto ait = art->fields.find("attack_animations");
						if (ait != art->fields.end() && !ait->second.empty()) {
							combo.clear();
							std::istringstream ss(ait->second);
							std::string name;
							while (ss >> name) combo.push_back(name);
						}
					}
				}
				m_attackAnim.setCombo(combo);
				m_comboItemId = itemId;
			}

			// Item attack stats
			float damage   = 1.5f;
			float cooldown = 0.4f;
			float range    = 2.5f;
			bool  canAttack = itemId.empty(); // bare fist always attacks

			if (!itemId.empty()) {
				const ArtifactEntry* entry = m_artifacts.findById(itemId);
				if (entry) {
					auto it = entry->fields.find("on_interact");
					if (it != entry->fields.end() && it->second == "attack") {
						canAttack = true;
						auto dit = entry->fields.find("damage");
						if (dit != entry->fields.end()) damage = std::stof(dit->second);
						auto cit = entry->fields.find("cooldown");
						if (cit != entry->fields.end()) cooldown = std::stof(cit->second);
						auto rit = entry->fields.find("range");
						if (rit != entry->fields.end()) range = std::stof(rit->second);
					}
				}
			}

			// Swing trigger: left-click with an attack-capable item, cooldown ready.
			bool canAirSwing = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			bool leftClick = (attackId != ENTITY_NONE) ||
			                 (canAirSwing && m_controls.held(Action::BreakBlock));
			if (leftClick && canAttack && m_attackCD <= 0) {
				if (m_attackAnim.trigger()) {
					m_attackCD = cooldown;
					if (itemId.empty())
						m_audio.play("hit_punch", pe->position, 0.35f);
					else
						m_audio.play("sword_swing", pe->position, 0.55f);
				}
			}

			// Send attack proposal only when a valid target is in range
			if (attackId != ENTITY_NONE && canAttack) {
				Entity* target = m_server->getEntity(attackId);
				if (target) {
					float dist = glm::length(target->position - pe->position);
					if (dist <= range) {
						ActionProposal p;
						p.type        = ActionProposal::Convert;
						p.actorId     = pe->id();
						p.convertFrom = Container::entity(attackId);
						p.fromItem    = "hp";
						p.fromCount   = (int)damage;
						p.toItem      = "";  // destroy HP
						m_server->sendAction(p);
						m_renderer.triggerHitmarker(false);
						m_combatFx.notifyHit();
					}
				}
			}
		}
	}

	// ── Item actions: Q=drop, E=equip, right-click=use ──
	if (pe->inventory && (m_state == GameState::PLAYING || m_state == GameState::ADMIN)) {
		int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
		std::string heldItem = m_hotbar.get(slot);

		// Q = drop selected item
		m_dropCooldown -= dt;
		if (m_controls.pressed(Action::DropItem) && !heldItem.empty() && pe->inventory->has(heldItem)) {
			ActionProposal p;
			p.type = ActionProposal::Relocate;
			p.actorId = m_server->controlledEntityId();
			p.relocateTo = Container::ground();
			p.itemId = heldItem;
			p.itemCount = 1;
			// Toss toward where the camera is looking
			p.desiredVel = m_camera.front() * 5.0f + glm::vec3(0, 3.0f, 0);
			m_server->sendAction(p);
			m_dropCooldown = 0.8f; // don't auto-pickup for 0.8s after dropping

			// Floating text: "-1 ItemName"
			const ArtifactEntry* dropArt = m_artifacts.findById(heldItem);
			std::string dropName = (dropArt && !dropArt->name.empty()) ? dropArt->name : [&]{
				std::string n = heldItem;
				auto c = n.find(':'); if (c != std::string::npos) n = n.substr(c + 1);
				if (!n.empty()) n[0] = (char)toupper((unsigned char)n[0]);
				for (auto& ch : n) if (ch == '_') ch = ' ';
				return n;
			}();
			std::string dropKey = heldItem;
			{ auto c = dropKey.find(':'); if (c != std::string::npos) dropKey = dropKey.substr(c + 1); }
			FloatTextEvent ft;
			ft.source      = FloatSource::Pickup;
			ft.worldPos    = pe->position + glm::vec3(0, 2.0f, 0);
			ft.coalesceKey = dropKey;
			ft.text        = dropName;
			ft.value       = -1.0f;
			m_floatText.add(ft);
		}

		// E = equip selected item
		if (m_controls.pressed(Action::EquipItem) && !heldItem.empty()) {
			const ArtifactEntry* art = m_artifacts.findById(heldItem);
			if (art) {
				auto slotIt = art->fields.find("equip_slot");
				if (slotIt != art->fields.end()) {
					printf("[Equip] '%s' → slot '%s'\n", heldItem.c_str(), slotIt->second.c_str());
					ActionProposal p;
					p.type = ActionProposal::Relocate;
					p.actorId = m_server->controlledEntityId();
					p.itemId = heldItem;
					p.equipSlot = slotIt->second;
					m_server->sendAction(p);
					// Metal items (sword, shield, helmet) use chain clink; others use cloth
					bool isMetal = heldItem.find("sword") != std::string::npos
					            || heldItem.find("shield") != std::string::npos
					            || heldItem.find("helmet") != std::string::npos
					            || heldItem.find("boots") != std::string::npos;
					m_audio.play(isMetal ? "item_equip_metal" : "item_equip", 0.55f);
				}
			}
		}

		// Right-click: use/eat/drink item (on_use = consume)
		// Only fires when not aiming at a block (block place takes priority).
		m_useCooldown -= dt;
		if (m_controls.pressed(Action::PlaceBlock) && !heldItem.empty()
		    && pe->inventory->has(heldItem) && !m_gameplay.currentHit()
		    && m_useCooldown <= 0) {
			const ArtifactEntry* art = m_artifacts.findById(heldItem);
			if (art) {
				auto usIt = art->fields.find("on_use");
				if (usIt != art->fields.end() && usIt->second == "consume") {
					auto cit = art->fields.find("cooldown");
					m_useCooldown = (cit != art->fields.end()) ? std::stof(cit->second) : 0.5f;

					ActionProposal p;
					p.type    = ActionProposal::Convert;
					p.actorId = m_server->controlledEntityId();
					p.fromItem  = heldItem;
					p.fromCount = 1;
					p.toItem    = "hp";
					auto eit = art->fields.find("effect_amount");
					p.toCount = (int)((eit != art->fields.end()) ? std::stof(eit->second) : 4.0f);
					m_server->sendAction(p);
					m_audio.play("item_consume", pe->position, 0.7f);

					// Floating text: "-1 PotionName"
					std::string consumeKey = heldItem;
					{ auto c = consumeKey.find(':'); if (c != std::string::npos) consumeKey = consumeKey.substr(c + 1); }
					FloatTextEvent ft;
					ft.source      = FloatSource::Pickup;
					ft.worldPos    = pe->position + glm::vec3(0, 2.0f, 0);
					ft.coalesceKey = consumeKey;
					ft.text        = art->name.empty() ? consumeKey : art->name;
					ft.value       = -1.0f;
					m_floatText.add(ft);
				}
			}
		}
	}
}

void Game::updateItemPickupAnimations(float dt) {
	Entity* pe = playerEntity();
	if (!pe) return;

	// Client-initiated item pickup: scan nearby items, send PickupItem action.
	// Skip scan briefly after dropping an item to prevent instant re-pickup.
	if (m_dropCooldown <= 0)
	{
		float pickupRange = pe->def().pickup_range;
		auto& srv = *m_server;
		EntityId playerId = srv.controlledEntityId();
		srv.forEachEntity([&](Entity& e) {
			if (e.typeId() != ItemName::ItemEntity) return;
			if (e.removed) return;
			if (m_pendingPickups.count(e.id())) return;
			float dist = glm::length(e.position - pe->position);
			if (dist >= pickupRange) return;
			// Shared capacity check — same logic the server will run on
			// its side. If we can't carry it, skip so the item stays visible.
			std::string itemType = e.getProp<std::string>(Prop::ItemType);
			int count = e.getProp<int>(Prop::Count, 1);
			if (pe->inventory && !pe->inventory->canAccept(itemType, count,
			                                                pe->def().inventory_capacity))
				return;
			{
				ActionProposal p;
				p.type = ActionProposal::Relocate;
				p.actorId = playerId;
				p.relocateFrom = Container::entity(e.id());
				srv.sendAction(p);
				m_pendingPickups.insert(e.id());

				// Start fly-toward-player animation (optimistic, client-side)
				const BlockDef* bdef = srv.blockRegistry().find(itemType);
				glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
				// Model key for rendering during fly animation
				std::string mk = itemType;
				auto mkColon = mk.find(':');
				if (mkColon != std::string::npos) mk = mk.substr(mkColon + 1);
				// Display name
				std::string name = mk;
				if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
				for (auto& c : name) if (c == '_') c = ' ';
				m_pickupAnims.push_back({e.id(), e.position, color, name, mk, count, 0, 0.35f});
			}
		});
		// Clean stale pending entries
		for (auto it = m_pendingPickups.begin(); it != m_pendingPickups.end(); ) {
			Entity* check = srv.getEntity(*it);
			if (!check || check->removed) it = m_pendingPickups.erase(it);
			else ++it;
		}
	}

	// Update pickup animations — animate item toward player, then fire effects
	for (auto it = m_pickupAnims.begin(); it != m_pickupAnims.end(); ) {
		it->t += dt / it->duration;
		if (it->t >= 1.0f) {
			// Arrived at player — puff + sound + HUD text
			m_particles.emitItemPickup(pe->position + glm::vec3(0, 0.8f, 0), it->color);
			m_audio.play("item_pickup", pe->position, 0.5f);

			FloatTextEvent ft;
			ft.source      = FloatSource::Pickup;
			ft.worldPos    = pe->position + glm::vec3(0, 2.0f, 0);
			ft.coalesceKey = it->modelKey;   // raw type key for deduplication
			ft.text        = it->itemName;   // display name, already formatted
			ft.value       = (float)it->count;
			m_floatText.add(ft);

			it = m_pickupAnims.erase(it);
		} else {
			++it;
		}
	}
}

void Game::updateAudioAndDoors(float dt) {
	Entity* pe = playerEntity();
	if (!pe) return;

	// Creature ambient sounds: very rare, within 5 blocks only, whisper-quiet.
	m_creatureSoundTimer -= dt;
	if (m_creatureSoundTimer <= 0) {
		m_creatureSoundTimer = 30.0f + (float)(rand() % 200) / 10.0f; // every 30-50s

		struct SoundCandidate { std::string group; glm::vec3 pos; float dist; };
		std::vector<SoundCandidate> candidates;
		m_server->forEachEntity([&](Entity& e) {
			float dist = glm::length(e.position - pe->position);
			if (dist > 5.0f || dist < 0.3f) return;
			const auto& tid = e.typeId();
			if (tid == "pig")          candidates.push_back({"creature_pig", e.position, dist});
			else if (tid == "chicken") candidates.push_back({"creature_chicken", e.position, dist});
			else if (tid == "dog")     candidates.push_back({"creature_dog", e.position, dist});
			else if (tid == "cat")     candidates.push_back({"creature_cat", e.position, dist});
		});
		if (!candidates.empty()) {
			auto& c = candidates[rand() % candidates.size()];
			// Volume fades with distance: 0.04 at 0 blocks, ~0 at 5 blocks
			float vol = 0.04f * (1.0f - c.dist / 5.0f);
			m_audio.play(c.group, c.pos, vol);
		}
	}

	// Door toggle sound + swing animation
	if (m_gameplay.doorToggled()) {
		m_audio.play("door_open", m_gameplay.doorTogglePos(), 0.6f);

		// Start door swing animation
		glm::vec3 fp = m_gameplay.doorTogglePos();
		glm::ivec3 bp{(int)std::floor(fp.x), (int)std::floor(fp.y), (int)std::floor(fp.z)};
		auto& chunks = m_server->chunks();
		auto& blocks = m_server->blockRegistry();
		BlockId bid = chunks.getBlock(bp.x, bp.y, bp.z);
		const BlockDef& bdef = blocks.get(bid);
		bool wasOpen = (bdef.string_id == BlockType::DoorOpen);

		// Read param2 hinge bit from the chunk
		ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
		Chunk* ch = chunks.getChunk(cp);
		uint8_t p2 = ch ? ch->getParam2(((bp.x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
		                                 ((bp.y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE,
		                                 ((bp.z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE) : 0;
		bool hingeRight = (p2 >> 2) & 1;
		glm::vec3 col = bdef.color_side;

		// Find bottom of door column
		glm::ivec3 base = bp;
		while (true) {
			BlockId below = chunks.getBlock(base.x, base.y - 1, base.z);
			const std::string& bs = blocks.get(below).string_id;
			if (bs == BlockType::Door || bs == BlockType::DoorOpen) base.y--;
			else break;
		}
		// Count height of door column
		int h = 0;
		for (int dy = 0; dy < 8; dy++) {
			BlockId above = chunks.getBlock(base.x, base.y + dy, base.z);
			const std::string& as2 = blocks.get(above).string_id;
			if (as2 == BlockType::Door || as2 == BlockType::DoorOpen) h++;
			else break;
		}
		if (h < 1) h = 1;

		m_doorAnims.push_back({base, h, 0.0f, !wasOpen, hingeRight, col});
		m_gameplay.clearDoorToggle();
	}

	// Advance door animations and remove finished ones
	{
		for (auto& a : m_doorAnims)
			a.timer += dt;
		m_doorAnims.erase(
			std::remove_if(m_doorAnims.begin(), m_doorAnims.end(),
				[](const DoorAnim& a) { return a.timer >= 0.25f; }),
			m_doorAnims.end());
	}
}

// ============================================================
// updatePlaying — orchestrates all playing-state helpers
// ============================================================
void Game::updatePlaying(float dt, float aspect) {
	if (handleConnectionReconnect(dt)) return;

	Entity* pe = playerEntity();
	if (!pe) {
		// Player entity not received yet — wait for server to broadcast it.
		m_connectTimer += dt;
		if (m_connectTimer > 10.0f) {
			printf("[Game] Timeout waiting for player entity\n");
			enterDisconnected("timed out waiting for player entity");
			return;
		}
		// Show loading message
		m_ui.beginFrame();
		ImGui::SetNextWindowPos(ImVec2(m_window.width() * 0.5f - 100, m_window.height() * 0.5f - 20));
		ImGui::Begin("##connecting", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Connecting to server...");
		ImGui::End();
		m_ui.endFrame();
		return;
	}
	m_connectTimer = 0;

	// Floating text update
	m_floatText.update(dt, m_camera.mode);

	// Debug capture tick — runs scenario and auto-exits when done
	if (m_debugCapture.active()) {
		development::ScenarioCallbacks cb;

		cb.save = [this](const std::string& path) {
			// Write a PPM screenshot directly (mirrors writeScreenshot in game.cpp)
			int w = m_window.width(), h = m_window.height();
			std::vector<uint8_t> px(w * h * 3);
			glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
			std::vector<uint8_t> fl(w * h * 3);
			for (int y = 0; y < h; y++)
				memcpy(&fl[y * w * 3], &px[(h - 1 - y) * w * 3], w * 3);
			std::ofstream f(path, std::ios::binary);
			f << "P6\n" << w << " " << h << "\n255\n";
			f.write((char*)fl.data(), fl.size());
			printf("Screenshot: %s\n", path.c_str());
		};
		cb.cycleCamera = [this]() {
			m_camera.cycleMode();
			m_camera.resetMouseTracking();
			// Center RTS/RPG camera on current player position when entering those modes
			if (m_camera.mode == CameraMode::RTS)
				m_camera.rtsCenter = m_camera.player.feetPos;
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		};
		cb.setCamera = [this](CameraMode mode) {
			while (m_camera.mode != mode) {
				m_camera.cycleMode();
				m_camera.resetMouseTracking();
			}
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		};
		cb.selectSlot = [pe](int slot) {
			pe->setProp(Prop::SelectedSlot, slot);
		};
		cb.dropItem = [this, pe]() {
			int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
			std::string itemId = pe->inventory ? m_hotbar.get(slot) : "";
			if (!itemId.empty() && pe->inventory->has(itemId)) {
				ActionProposal drop;
				drop.type = ActionProposal::Relocate;
				drop.actorId = m_server->controlledEntityId();
				drop.itemId = itemId;
				drop.itemCount = 1;
				drop.relocateTo = Container::ground();
				drop.desiredVel = m_debugCapture.active()
					? glm::vec3(std::cos(glm::radians(pe->yaw)) * 1.0f, 1.5f,
					            std::sin(glm::radians(pe->yaw)) * 1.0f)
					: m_camera.front() * 3.0f + glm::vec3(0, 2.0f, 0);
				m_server->sendAction(drop);
				m_dropCooldown = 0.8f;
			}
		};
		cb.triggerSwing = [this]() {
			m_attackAnim.triggerOnce("swing_right");
		};
		cb.hotbar = &m_hotbar;
		cb.setCharacterSkin = [pe](const std::string& skinId) {
			pe->setProp("character_skin", skinId);
		};
		cb.setPlayerYaw = [pe, this](float yawDeg) {
			pe->yaw = yawDeg;
			m_camera.player.yaw = yawDeg;
		};
		cb.setRPGCameraOrbit = [this](float orbitYaw, float angle, float dist) {
			m_camera.godOrbitYaw     = orbitYaw;
			m_camera.godAngle        = angle;
			m_camera.godDistance     = dist;
			m_camera.godDistanceTarget = dist;
		};
		// RPG aim target = feetPos + eyeHeight*0.8; override eyeHeight so
		// small characters are framed at their center, not at player eye level.
		cb.setCameraAimHeight = [this](float h) {
			m_camera.player.eyeHeight = h / 0.8f;
		};
		cb.setPlayerClip = [this](const std::string& clip) {
			m_playerClip = clip;
		};
		cb.setPlayerAnimTime = [this](float t) {
			m_debugAnimTime = t;
		};
		cb.setPlayerWalk = [this](float phase, float speed) {
			m_debugWalkPhase = phase;
			m_debugWalkSpeed = speed;
		};
		cb.triggerAttackClip = [this](const std::string& clipId) {
			m_attackAnim.triggerOnce(clipId);
		};

		m_debugCapture.tick(dt, pe, m_camera, cb);
		if (m_debugCapture.done()) {
			printf("[Debug] Scenario complete — exiting.\n");
			glfwSetWindowShouldClose(m_window.handle(), true);
		}
	}

	if (m_controls.pressed(Action::MenuBack)) {
		// ESC closes overlays first, then shows pause menu
		if (m_chestUI.isOpen()) {
			m_chestUI.close();
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			return;
		}
		if (m_equipUI.isOpen()) {
			m_equipUI.close();
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			return;
		}
		m_preMenuState = m_state;
		m_state = GameState::PAUSED;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		return;
	}

	using Clock = std::chrono::steady_clock;
	auto _tP0 = Clock::now();
	handleGameplayInput(dt);
	auto _tP1 = Clock::now();

	// Camera tracks entity position — same for all modes.
	m_camera.player.feetPos = pe->position;
	// Player model yaw: FPS = look direction, all others = entity yaw (smooth)
	if (m_camera.mode != CameraMode::FirstPerson) {
		float diff = pe->yaw - m_camera.player.yaw;
		while (diff > 180.0f) diff -= 360.0f;
		while (diff < -180.0f) diff += 360.0f;
		// Faster tracking (20x) for snappy turning; still smooth enough to avoid pops
		m_camera.player.yaw += diff * std::min(dt * 20.0f, 1.0f);
	}
	m_worldTime = m_server->worldTime();
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);

	// Update audio listener + background music
	m_audio.setListener(m_camera.position, m_camera.front());
	m_audio.updateMusic();

	auto _tP2 = Clock::now();
	updateItemPickupAnimations(dt);
	auto _tP3 = Clock::now();
	updateAudioAndDoors(dt);
	auto _tP4 = Clock::now();

	// Tick NPC agents (owned NPCs running Python behaviors in-process)
	if (m_agentClient)
		m_agentClient->tick(dt);
	auto _tP5 = Clock::now();

	// Block place feedback (immediate client-side sound)
	auto& placeEvt = m_gameplay.placeEvent();
	if (placeEvt.happened) {
		std::string snd = "place_stone";
		const std::string& bt = placeEvt.blockType;
		if (bt.find("wood") != std::string::npos || bt.find("log") != std::string::npos)
			snd = "place_wood";
		else if (bt.find("dirt") != std::string::npos || bt.find("sand") != std::string::npos)
			snd = "place_soft";
		m_audio.play(snd, placeEvt.pos, 0.5f);
	}

	// Per-hit mining feedback (particles + sound on each survival swing)
	auto& hitEvt = m_gameplay.hitEvent();
	if (hitEvt.happened) {
		m_particles.emitBlockBreak(hitEvt.pos, hitEvt.color, 5);
		float r = hitEvt.color.r, g = hitEvt.color.g, b = hitEvt.color.b;
		if (r > 0.7f && g > 0.7f && b > 0.7f)
			m_audio.play("dig_snow", hitEvt.pos, 0.4f);
		else if (r < 0.5f && g < 0.5f && b < 0.5f)
			m_audio.play("dig_stone", hitEvt.pos, 0.5f);
		else if (g > r && g > b)
			m_audio.play("dig_leaves", hitEvt.pos, 0.3f);
		else
			m_audio.play("dig_dirt", hitEvt.pos, 0.4f);
	}

	// ── Chest open: right-click on a chest block ──
	auto& chestEvt = m_gameplay.chestOpenEvent();
	if (chestEvt.happened) {
		// Locate the Structure entity for this chest block (spawned at block center).
		glm::ivec3 bp = chestEvt.blockPos;
		glm::vec3 blockCenter = {bp.x + 0.5f, bp.y + 0.5f, bp.z + 0.5f};
		EntityId chestEid = ENTITY_NONE;
		float bestDist = 0.5f; // must be very near the center
		m_server->forEachEntity([&](Entity& e) {
			if (e.typeId() != StructureName::Chest) return;
			float d = glm::length(e.position - blockCenter);
			if (d < bestDist) { bestDist = d; chestEid = e.id(); }
		});
		if (chestEid != ENTITY_NONE) {
			// Request the latest inventory snapshot and open the UI.
			m_server->sendGetInventory(chestEid);
			m_chestUI.setModels(&m_models, &m_iconCache);
			m_chestUI.open(chestEid);
			m_chestUI.setTransferCallback(
				[this](bool chestToPlayer, const std::string& itemId, int count) {
					EntityId eid = m_chestUI.chestEntityId();
					if (eid == ENTITY_NONE) return;
					Entity* chestE = m_server->getEntity(eid);
					Entity* pe2    = playerEntity();
					if (!chestE || !pe2 || !chestE->inventory || !pe2->inventory) return;

					// count == 0 means "move all": look up source stack size now.
					int available = chestToPlayer
						? chestE->inventory->count(itemId)
						: pe2->inventory->count(itemId);
					int n = (count <= 0) ? available : std::min(count, available);
					if (n <= 0) return;

					ActionProposal p;
					p.type      = ActionProposal::Relocate;
					p.actorId   = m_server->controlledEntityId();
					p.itemId    = itemId;
					p.itemCount = n;
					if (chestToPlayer) {
						p.relocateFrom = Container::entity(eid);
						p.relocateTo   = Container::self();
					} else {
						p.relocateFrom = Container::self();
						p.relocateTo   = Container::entity(eid);
					}
					m_server->sendAction(p);
					// Refresh chest view after the move lands on the server.
					m_server->sendGetInventory(eid);
				});
			// Release mouse for UI interaction
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
		m_gameplay.clearChestOpenEvent();
	}

	// Auto-close the chest UI when the player walks too far or the chest vanishes.
	if (m_chestUI.isOpen()) {
		EntityId eid = m_chestUI.chestEntityId();
		Entity* chestE = m_server->getEntity(eid);
		if (!chestE || chestE->removed) {
			m_chestUI.close();
		} else {
			float d = glm::length(chestE->position - pe->position);
			if (d > 6.0f) {
				m_chestUI.close();
				bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
				                    m_camera.mode == CameraMode::ThirdPerson);
				glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
					needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			}
		}
	}

	// Check if player right-clicked an entity → enter inspection
	if (m_gameplay.inspectedEntity() != ENTITY_NONE) {
		m_preInspectState = m_state;
		m_state = GameState::ENTITY_INSPECT;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking(); // prevent jump when returning to gameplay
	}

	auto _tP6 = Clock::now();
	renderPlaying(dt, aspect);
	auto _tP7 = Clock::now();

	auto _ms = [](Clock::duration d) {
		return std::chrono::duration<float, std::milli>(d).count();
	};
	float tot = _ms(_tP7 - _tP0);
	if (tot > 33.0f) {
		static int slowUpdCount = 0;
		slowUpdCount++;
		if (slowUpdCount <= 5 || slowUpdCount % 60 == 0) {
			fprintf(stderr, "[PerfUpd] total=%.1fms  input=%.1f cam/audio=%.1f pickups=%.1f doors=%.1f agents=%.1f misc=%.1f render=%.1f\n",
				tot,
				_ms(_tP1 - _tP0), _ms(_tP2 - _tP1), _ms(_tP3 - _tP2),
				_ms(_tP4 - _tP3), _ms(_tP5 - _tP4), _ms(_tP6 - _tP5),
				_ms(_tP7 - _tP6));
		}
	}
}

} // namespace civcraft

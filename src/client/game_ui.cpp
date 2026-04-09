#include "client/game.h"
#include "imgui.h"
#include <sstream>

namespace modcraft {

// ============================================================
// Entity inspection overlay
// ============================================================
void Game::updateEntityInspect(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }
	Entity* pe = playerEntity();
	if (!pe) { m_state = GameState::MENU; return; }

	// Keep server running
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Render 3D world in background (skip ImGui overlays — we draw our own)
	m_globalTime += dt;
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);
	renderPlaying(dt, aspect, true);

	// Get inspected entity
	EntityId eid = m_gameplay.inspectedEntity();
	Entity* target = m_server->getEntity(eid);
	if (!target) { m_gameplay.clearInspection(); return; }

	// ── ImGui overlay panel ──────────────────────────────────
	m_ui.beginFrame();

	float ww = (float)m_window.width(), wh = (float)m_window.height();
	float panW = std::min(520.0f, ww * 0.85f);
	float panH = std::min(680.0f, wh * 0.88f);
	ImGui::SetNextWindowPos({(ww - panW) * 0.5f, (wh - panH) * 0.5f}, ImGuiCond_Always);
	ImGui::SetNextWindowSize({panW, panH}, ImGuiCond_Always);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.94f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 10});

	char title[128];
	snprintf(title, sizeof(title), "%s  (%s  #%u)###EntityInspect",
		target->def().display_name.c_str(), target->typeId().c_str(), (unsigned)eid);

	bool open = true;
	if (ImGui::Begin(title, &open,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {

		// ── Live stats ──
		int hp = target->getProp<int>(Prop::HP, target->def().max_hp);
		int maxHp = target->def().max_hp;
		ImGui::TextColored({0.4f, 1.0f, 0.4f, 1}, "HP: %d / %d", hp, maxHp);
		ImGui::SameLine(200);
		ImGui::TextColored({0.7f, 0.7f, 0.7f, 1}, "Pos: %.1f, %.1f, %.1f",
			target->position.x, target->position.y, target->position.z);
		ImGui::TextColored({0.55f, 0.75f, 1.0f, 1}, "Entity ID: %u", (unsigned)eid);
		ImGui::SameLine(200);
		ImGui::TextColored({0.65f, 0.65f, 0.70f, 1}, "Type: %s", target->typeId().c_str());

		ImGui::TextColored({0.5f, 1.0f, 0.8f, 1}, "> %s",
			target->goalText.empty() ? "(pending)" : target->goalText.c_str());
		if (target->hasError) {
			ImGui::TextColored({1.0f, 0.3f, 0.3f, 1}, "ERROR: %s", target->errorText.c_str());
		}

		// Ownership display + claim button
		{
			int owner = target->getProp<int>(Prop::Owner, 0);
			EntityId myId = m_server->localPlayerId();
			bool isMine = (owner == (int)myId);
			bool isAdmin = (m_state == GameState::ADMIN);
			if (isMine) {
				ImGui::TextColored({0.3f, 0.9f, 0.3f, 1}, "Owned by you");
			} else if (owner == 0) {
				ImGui::TextColored({0.6f, 0.6f, 0.6f, 1}, "Unclaimed");
				ImGui::SameLine();
				if (ImGui::SmallButton("Claim")) {
					m_server->sendClaimEntity(eid);
				}
			} else {
				ImGui::TextColored({0.8f, 0.6f, 0.3f, 1}, "Owned by entity %d", owner);
				if (isAdmin) {
					ImGui::SameLine();
					if (ImGui::SmallButton("Claim (Admin)")) {
						m_server->sendClaimEntity(eid);
					}
				}
			}
		}

		ImGui::Separator();

		// ── Properties ──
		if (ImGui::CollapsingHeader("Properties")) {
			auto& def = target->def();
			if (ImGui::BeginTable("Props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130);
				ImGui::TableSetupColumn("Value");
				auto row = [](const char* label, const char* fmt, auto... args) {
					ImGui::TableNextRow(); ImGui::TableNextColumn();
					ImGui::TextColored({0.72f, 0.74f, 0.80f, 1}, "%s", label);
					ImGui::TableNextColumn();
					char buf[64]; snprintf(buf, sizeof(buf), fmt, args...);
					ImGui::TextColored({0.92f, 0.95f, 1.00f, 1}, "%s", buf);
				};
				row("Walk Speed", "%.1f", def.walk_speed);
				row("Run Speed", "%.1f", def.run_speed);
				row("Max HP", "%d", def.max_hp);
				std::string bid = target->getProp<std::string>(Prop::BehaviorId, "");
				if (!bid.empty()) { row("Behavior", "%s", bid.c_str()); }
				ImGui::EndTable();
			}
		}

		// ── Inventory — hotbar-style rows with 3D icon ──────────────
		if (target->inventory) {
			auto items = target->inventory->items();
			char invHeader[64];
			int totalItems = 0;
			for (auto& [id, cnt] : items) totalItems += cnt;
			snprintf(invHeader, sizeof(invHeader), "Inventory  [%d items]###Inv", totalItems);
			if (ImGui::CollapsingHeader(invHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
				if (items.empty()) {
					ImGui::TextDisabled("  (empty)");
				} else {
					const float slotSz  = 44.0f;
					const float rowH    = slotSz + 6.0f;
					const float padX    = 8.0f;
					auto& blkReg = m_server->blockRegistry();
					ImDrawList* dl = ImGui::GetWindowDrawList();

					for (auto& [itemId, count] : items) {
						ImVec2 cursor = ImGui::GetCursorScreenPos();
						cursor.x += padX;

						// Slot background
						ImVec2 slotMin = cursor;
						ImVec2 slotMax = {cursor.x + slotSz, cursor.y + slotSz};
						dl->AddRectFilled(slotMin, slotMax,
							IM_COL32(28, 32, 50, 220), 5.0f);
						dl->AddRect(slotMin, slotMax,
							IM_COL32(80, 100, 160, 180), 5.0f, 0, 1.5f);

						// 3D model icon
						std::string modelKey = itemId;
						auto col = modelKey.find(':');
						if (col != std::string::npos) modelKey = modelKey.substr(col + 1);
						auto mit = m_models.find(modelKey);
						GLuint icon = (mit != m_models.end())
							? m_iconCache.getIcon(modelKey, mit->second) : 0;
						if (icon) {
							dl->AddImage((ImTextureID)(intptr_t)icon,
								{slotMin.x + 3, slotMin.y + 3},
								{slotMax.x - 3, slotMax.y - 3},
								{0, 1}, {1, 0});
						} else {
							// Fallback: coloured letter block
							const BlockDef* bdef = blkReg.find(itemId);
							ImVec4 col4 = bdef
								? ImVec4(bdef->color_top.x, bdef->color_top.y, bdef->color_top.z, 1)
								: ImVec4(0.6f, 0.6f, 0.6f, 1);
							dl->AddRectFilled({slotMin.x+4, slotMin.y+4},
								{slotMax.x-4, slotMax.y-4},
								ImGui::ColorConvertFloat4ToU32(col4), 3.0f);
						}

						// Count badge (bottom-right corner of slot)
						char cntBuf[8]; snprintf(cntBuf, sizeof(cntBuf), "%d", count);
						ImVec2 badgePos = {slotMax.x - ImGui::CalcTextSize(cntBuf).x - 3,
						                   slotMax.y - ImGui::GetTextLineHeight() - 2};
						dl->AddText(badgePos, IM_COL32(255, 220, 60, 255), cntBuf);

						// Item display name (friendly, to the right of icon)
						ImGui::SetCursorScreenPos({slotMax.x + 10.0f,
							cursor.y + (slotSz - ImGui::GetTextLineHeightWithSpacing()) * 0.5f});
						const BlockDef* bdef = blkReg.find(itemId);
						std::string dispName = bdef && !bdef->display_name.empty()
							? bdef->display_name : itemId;
						ImGui::TextColored({0.88f, 0.92f, 1.0f, 1.0f}, "%s", dispName.c_str());

						// Advance cursor past row
						ImGui::SetCursorScreenPos({cursor.x - padX, cursor.y + rowH});
						ImGui::Dummy({0, 2});
					}
				}
			}
		}

		// ── Behavior Tree Editor ──
		if (ImGui::CollapsingHeader("Behavior Tree")) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.15f, 1));
			ImGui::BeginChild("##behaviorTree", ImVec2(0, 200), true);

			int idCounter = 0;
			BehaviorExprEditor::render(m_inspectEditor.sharedBehavior, 0, idCounter);

			ImGui::EndChild();
			ImGui::PopStyleColor();

			// Python preview (compiled from tree)
			if (ImGui::TreeNode("Python Preview")) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
				ImGui::BeginChild("##pyPreview", ImVec2(0, 150), true);
				std::istringstream stream(code);
				std::string line;
				int lineNum = 1;
				while (std::getline(stream, line)) {
					ImGui::TextColored({0.38f, 0.45f, 0.55f, 1}, "%3d ", lineNum++);
					ImGui::SameLine();
					// Simple syntax coloring
					if (line.find("def ") != std::string::npos || line.find("if ") != std::string::npos ||
					    line.find("else") != std::string::npos || line.find("for ") != std::string::npos)
						ImGui::TextColored({0.55f, 0.78f, 1.0f, 1}, "%s", line.c_str());
					else if (line.find("return ") != std::string::npos || line.find("import ") != std::string::npos)
						ImGui::TextColored({0.85f, 0.55f, 1.0f, 1}, "%s", line.c_str());
					else if (line.empty() || line[0] == '#')
						ImGui::TextColored({0.50f, 0.72f, 0.50f, 1}, "%s", line.c_str());
					else
						ImGui::TextColored({0.88f, 0.92f, 0.82f, 1}, "%s", line.c_str());
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::TreePop();
			}

			// ── Apply buttons with scope ──
			ImGui::Spacing();
			std::string typeName = target->def().display_name;

			char applyOneLabel[64], applyAllLabel[64];
			snprintf(applyOneLabel, sizeof(applyOneLabel), "Apply to This %s", typeName.c_str());
			snprintf(applyAllLabel, sizeof(applyAllLabel), "Apply to All %ss", typeName.c_str());

			if (ImGui::Button(applyOneLabel)) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				char filename[64];
				snprintf(filename, sizeof(filename), "entity_%u_behavior", eid);
				m_behaviorStore.save(filename, code);
				ActionProposal reload;
				reload.actorId = eid;
				reload.behaviorSource = code;
				m_server->sendAction(reload);
				printf("[Inspect] Applied behavior to entity %u only\n", eid);
			}
			ImGui::SameLine();
			if (ImGui::Button(applyAllLabel)) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				uint32_t hash = 0;
				for (char c : code) hash = hash * 31 + (uint8_t)c;
				char behaviorName[32];
				snprintf(behaviorName, sizeof(behaviorName), "custom_%06x", hash & 0xFFFFFF);
				m_behaviorStore.save(behaviorName, code);
				m_server->forEachEntity([&](Entity& e) {
					if (e.typeId() == target->typeId()) {
						ActionProposal reload;
						reload.actorId = e.id();
						reload.behaviorSource = code;
						m_server->sendAction(reload);
					}
				});
				printf("[Inspect] Applied behavior '%s' to all %s entities\n",
					behaviorName, target->typeId().c_str());
			}
		}

		// ── Current behavior source (read-only reference) ──
		auto behaviorInfo = m_server->getBehaviorInfo(eid);
		if (!behaviorInfo.sourceCode.empty()) {
			if (ImGui::CollapsingHeader("Current Behavior Source")) {
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
				ImGui::BeginChild("##curSrc", ImVec2(0, 120), true);
				std::istringstream stream(behaviorInfo.sourceCode);
				std::string line;
				int lineNum = 1;
				while (std::getline(stream, line)) {
					ImGui::TextColored({0.45f, 0.50f, 0.45f, 1}, "%3d ", lineNum++);
					ImGui::SameLine();
					ImGui::TextColored({0.35f, 0.65f, 0.35f, 1}, "%s", line.c_str());
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();

	m_ui.endFrame();

	// ESC or X button closes
	if (!open || m_controls.pressed(Action::MenuBack)) {
		m_gameplay.clearInspection();
		m_state = m_preInspectState;
		bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
		                    m_camera.mode == CameraMode::ThirdPerson);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking();
	}
}

// ============================================================
// Code editor overlay
// ============================================================
void Game::updateCodeEditor(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }

	// Keep server running (other clients / AI behaviors shouldn't freeze)
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	m_globalTime += dt;

	// Render 3D world in background (skip ImGui — code editor has its own)
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);
	renderPlaying(dt, aspect, true);

	// Render code editor on top
	m_codeEditor.render(m_text, aspect, m_globalTime);

	// Check editor actions
	if (m_codeEditor.wantsCancel()) {
		m_codeEditor.close();
		m_state = GameState::ENTITY_INSPECT;
		m_codeEditor.clearFlags();
	}

	if (m_codeEditor.wantsApply()) {
		std::string newCode = m_codeEditor.getCode();
		EntityId eid = m_codeEditor.editingEntity();

		// Save to artifacts/ (persists across restarts)
		char filename[64];
		snprintf(filename, sizeof(filename), "entity_%u_behavior", eid);
		m_behaviorStore.save(filename, newCode);

		// Send behavior reload request to server → forwarded to bot client
		ActionProposal reload;
		reload.actorId = eid;
		reload.behaviorSource = newCode;
		m_server->sendAction(reload);
		printf("[CodeEditor] Behavior reload sent for entity %u\n", eid);

		m_codeEditor.clearError();
		m_codeEditor.close();
		m_state = GameState::ENTITY_INSPECT;
		m_codeEditor.clearFlags();
	}

	if (m_codeEditor.wantsReset()) {
		EntityId eid = m_codeEditor.editingEntity();
		auto info = m_server->getBehaviorInfo(eid);
		m_codeEditor.open(eid, info.sourceCode, info.goal);
		m_codeEditor.clearFlags();
	}
}

// ============================================================
// Pause menu overlay (Esc during gameplay)
// ============================================================
void Game::updatePaused(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }

	// Game keeps running (DST/Minecraft multiplayer style — no pause)
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Render the world behind the overlay — but NOT the ImGui frame
	// from renderPlaying (we need our own single ImGui frame for the
	// pause overlay buttons to actually receive input).
	{
		auto& srv = *m_server;
		Entity* pe = playerEntity();
		if (!pe) { m_state = GameState::MENU; return; }
		m_globalTime += dt;
		m_worldTime = m_server->worldTime();
		m_renderer.setTimeOfDay(m_worldTime);
		m_renderer.tick(dt);
		m_renderer.updateChunks(srv.chunks(), m_camera, m_renderDistance);
		glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
		m_renderer.render(m_camera, aspect, nullptr, 0, 7, {0,0}, false);
		// Draw entities — same resolveModelKey as renderPlaying
		auto& mr = m_renderer.modelRenderer();
		auto resolveKey = [](const Entity& e) -> std::string {
			std::string skin = e.getProp<std::string>("character_skin", "");
			if (!skin.empty()) {
				auto colon = skin.find(':');
				return (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
			}
			std::string key = e.def().model;
			auto dot = key.rfind('.'); if (dot != std::string::npos) key = key.substr(0, dot);
			return key;
		};
		srv.forEachEntity([&](Entity& e) {
			if (e.typeId() == EntityType::ItemEntity) return;
			auto it = m_models.find(resolveKey(e));
			if (it != m_models.end()) {
				float spd = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
				AnimState anim = {e.getProp<float>(Prop::WalkDistance, 0.0f), spd, m_globalTime};
				mr.draw(it->second, vp, e.position, e.yaw, anim);
			}
		});
		m_particles.update(dt);
		m_particles.render(vp);

		// Restore GL state before ImGui
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
	}

	// Single ImGui frame for the pause overlay
	m_ui.beginFrame();

	// Dim overlay
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled({0, 0}, {(float)m_window.width(), (float)m_window.height()},
		IM_COL32(0, 0, 0, 140));

	float pw = 360, ph = 440;
	float px = (m_window.width() - pw) * 0.5f;
	float py = (m_window.height() - ph) * 0.5f;

	ImGui::SetNextWindowPos({px, py});
	ImGui::SetNextWindowSize({pw, ph});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.98f, 0.97f, 0.96f, 0.98f));
	ImGui::Begin("##gamemenu", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

	// Title
	ImGui::SetWindowFontScale(1.3f);
	float titleW = ImGui::CalcTextSize("Game Menu").x;
	ImGui::SetCursorPosX((pw - titleW) * 0.5f - 20);
	ImGui::TextColored({0.25f, 0.25f, 0.28f, 1.0f}, "Game Menu");
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

	float btnW = pw - 44;
	auto styledButton = [&](const char* label, ImVec4 bg, ImVec4 bgHover, ImVec4 bgActive,
	                        ImVec4 text, float height = 42.0f) {
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, bg);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgActive);
		ImGui::PushStyleColor(ImGuiCol_Text, text);
		ImGui::SetCursorPosX(22);
		bool clicked = ImGui::Button(label, {btnW, height});
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar();
		return clicked;
	};

	// Back to Game
	if (styledButton("Back to Game",
		{0.96f, 0.65f, 0.15f, 1}, {0.98f, 0.72f, 0.28f, 1}, {0.90f, 0.55f, 0.10f, 1},
		{1, 1, 1, 1}) || m_controls.pressed(Action::MenuBack)) {
		m_state = m_preMenuState;
		bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
		                    m_camera.mode == CameraMode::ThirdPerson);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking();
	}
	ImGui::Spacing();

	// Go to Main Menu (keeps game running, can resume)
	if (styledButton("Go to Main Menu",
		{0.40f, 0.42f, 0.48f, 1}, {0.50f, 0.52f, 0.58f, 1}, {0.35f, 0.37f, 0.42f, 1},
		{1, 1, 1, 1})) {
		m_state = GameState::MENU;
		m_imguiMenu.setGameRunning(true); // game still running, can resume
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	ImGui::Spacing();

	// Game Log toggle
	{
		const char* logLabel = m_showGameLog ? "Hide Game Log" : "Game Log";
		if (styledButton(logLabel,
			{0.18f, 0.42f, 0.55f, 1}, {0.24f, 0.52f, 0.66f, 1}, {0.14f, 0.36f, 0.48f, 1},
			{1, 1, 1, 1})) {
			m_showGameLog = !m_showGameLog;
		}
		ImGui::Spacing();
	}

	// Display settings
	ImGui::Separator(); ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.40f, 1.0f));
	ImGui::SetCursorPosX(22);
	ImGui::TextUnformatted("Display");
	ImGui::PopStyleColor();
	ImGui::Spacing();
	ImGui::SetCursorPosX(28);
	ImGui::Checkbox("Show lightbulbs", &m_showGoalBubbles);
	ImGui::SetCursorPosX(28);
	ImGui::Checkbox("Show goal text",  &m_showGoalText);
	ImGui::Spacing();

	// Quit Game
	if (styledButton("Quit Game",
		{0.65f, 0.20f, 0.20f, 1}, {0.75f, 0.28f, 0.28f, 1}, {0.55f, 0.15f, 0.15f, 1},
		{1, 1, 1, 1})) {
		m_state = GameState::MENU;
		m_imguiMenu.setGameRunning(false);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_agentMgr.stopAll();
		m_server->disconnect();
		m_server.reset();
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	// ── Game Log panel ──────────────────────────────────────────────────────
	if (m_showGameLog && !m_gameLog.empty()) {
		float lw = 580, lh = 420;
		float lx = (m_window.width()  - lw) * 0.5f + 200;
		float ly = (m_window.height() - lh) * 0.5f;
		ImGui::SetNextWindowPos({lx, ly}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({lw, lh}, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.10f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
		ImGui::Begin("##gamelog", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

		ImGui::SetWindowFontScale(0.95f);
		ImGui::TextColored({0.55f, 0.75f, 0.95f, 1.0f}, "Game Log");
		ImGui::SameLine(lw - 70);
		ImGui::TextColored({0.4f, 0.4f, 0.45f, 1.0f}, "%d entries", (int)m_gameLog.size());
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild("##logscroll", {lw - 24, lh - 62}, false,
		                  ImGuiWindowFlags_HorizontalScrollbar);
		// Newest at bottom — iterate forward
		for (const auto& line : m_gameLog) {
			// Colour-code: deaths red, damage orange, AI decisions green, pickups gold
			glm::vec4 c = {0.75f, 0.78f, 0.82f, 0.9f};
			if (line.find("died")   != std::string::npos) c = {1.0f, 0.35f, 0.25f, 1.0f};
			else if (line.find("damage") != std::string::npos) c = {1.0f, 0.62f, 0.20f, 1.0f};
			else if (line.find("Picked") != std::string::npos) c = {1.0f, 0.90f, 0.30f, 1.0f};
			else if (line.find(": ")  != std::string::npos) c = {0.65f, 0.88f, 0.55f, 1.0f};
			ImGui::TextColored({c.r, c.g, c.b, c.a}, "%s", line.c_str());
		}
		// Auto-scroll to bottom
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	m_ui.endFrame();
	if (m_state != GameState::PAUSED) m_showGameLog = false; // close log when leaving pause
}

} // namespace modcraft

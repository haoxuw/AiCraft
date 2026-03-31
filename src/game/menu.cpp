#include "game/menu.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>

namespace aicraft {

// Main-menu button layout (matches original main.cpp)
struct MenuButton { float x, y, w, h; const char* label; GameState target; };

static MenuButton s_buttons[] = {
	{-0.28f,  0.20f, 0.56f, 0.11f, "Single Player", GameState::TEMPLATE_SELECT},
	{-0.28f,  0.05f, 0.56f, 0.11f, "Creative Mode", GameState::TEMPLATE_SELECT},
	{-0.28f, -0.10f, 0.56f, 0.11f, "Character",     GameState::CHARACTER},
	{-0.28f, -0.25f, 0.56f, 0.11f, "Controls",      GameState::CONTROLS},
	{-0.28f, -0.40f, 0.56f, 0.11f, "Quit",           GameState::MENU},
};
static const int BUTTON_COUNT = 5;

// -----------------------------------------------------------------
// init
// -----------------------------------------------------------------
void MenuSystem::init(const std::vector<std::shared_ptr<WorldTemplate>>& templates,
                      CharacterManager* characters, FaceLibrary* faces)
{
	m_templates = templates;
	m_characters = characters;
	m_faces = faces;
	m_prevClick = false;
	m_cooldown = 0.5f;
	m_pendingGameState = GameState::SURVIVAL;
}

// -----------------------------------------------------------------
// accessors
// -----------------------------------------------------------------
void MenuSystem::setPendingGameState(GameState gs) { m_pendingGameState = gs; }
GameState MenuSystem::pendingGameState() const { return m_pendingGameState; }
void MenuSystem::resetCooldown(float t) { m_cooldown = t; }

// -----------------------------------------------------------------
// MAIN MENU
// -----------------------------------------------------------------
MenuAction MenuSystem::updateMainMenu(float dt, Window& window, TextRenderer& text,
                                      ControlManager& /*controls*/, float aspect,
                                      bool demoMode, float autoScreenTimer)
{
	glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	m_cooldown -= dt;

	MenuAction result;

	bool click = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if (click && !m_prevClick && m_cooldown <= 0) {
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		float nx = (float)(mx / window.width()) * 2 - 1;
		float ny = 1 - (float)(my / window.height()) * 2;

		for (int i = 0; i < BUTTON_COUNT; i++) {
			auto& b = s_buttons[i];
			if (nx >= b.x && nx <= b.x + b.w && ny >= b.y && ny <= b.y + b.h) {
				// Quit button (last)
				if (i == BUTTON_COUNT - 1) {
					result.type = MenuAction::Quit;
					m_prevClick = click;
					return result;
				}
				// Controls
				if (b.target == GameState::CONTROLS) {
					result.type = MenuAction::ShowControls;
					m_prevClick = click;
					return result;
				}
				// Character
				if (b.target == GameState::CHARACTER) {
					result.type = MenuAction::ShowCharacter;
					m_prevClick = click;
					return result;
				}
				// Single Player (i==0) → Survival, Creative (i==1) → Creative
				m_pendingGameState = (i == 0) ? GameState::SURVIVAL : GameState::CREATIVE;
				m_cooldown = 0.3f;
				result.type = MenuAction::ShowTemplateSelect;
				m_prevClick = click;
				return result;
			}
		}
	}
	m_prevClick = click;

	// Demo: auto-enter creative with village template (index 1)
	if (demoMode && autoScreenTimer > 0.3f) {
		result.type = MenuAction::EnterGame;
		result.templateIndex = 1;
		result.targetState = GameState::CREATIVE;
		return result;
	}

	// ---- Draw ----
	glClearColor(0.15f, 0.18f, 0.22f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	text.drawTitle("AiCraft", -0.18f, 0.42f, 3.0f, {0.95f, 0.95f, 1.0f, 1}, aspect);
	text.drawText("v0.9.0", -0.07f, 0.30f, 1.0f, {0.5f, 0.5f, 0.6f, 1}, aspect);

	for (int i = 0; i < BUTTON_COUNT; i++) {
		auto& b = s_buttons[i];
		text.drawRect(b.x, b.y, b.w, b.h, {0.25f, 0.30f, 0.35f, 0.9f});
		float tw = strlen(b.label) * 0.018f * 1.2f;
		text.drawText(b.label, b.x + (b.w - tw) * 0.5f, b.y + 0.03f, 1.2f, {1, 1, 1, 1}, aspect);
	}

	window.swapBuffers();
	return result; // None
}

// -----------------------------------------------------------------
// TEMPLATE SELECT
// -----------------------------------------------------------------
MenuAction MenuSystem::updateTemplateSelect(float dt, Window& window, TextRenderer& text,
                                            ControlManager& controls, float aspect)
{
	glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	m_cooldown -= dt;

	MenuAction result;

	// Escape / MenuBack → back to main menu
	if (controls.pressed(Action::MenuBack)) {
		m_cooldown = 0.5f;
		result.type = MenuAction::BackToMenu;
		return result;
	}

	bool click = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if (click && !m_prevClick && m_cooldown <= 0) {
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		float nx = (float)(mx / window.width()) * 2 - 1;
		float ny = 1 - (float)(my / window.height()) * 2;

		float btnW = 0.56f, btnH = 0.12f, btnX = -0.28f;

		// Template buttons
		for (int i = 0; i < (int)m_templates.size(); i++) {
			float btnY = 0.14f - i * 0.18f;
			if (nx >= btnX && nx <= btnX + btnW && ny >= btnY && ny <= btnY + btnH) {
				result.type = MenuAction::EnterGame;
				result.templateIndex = i;
				result.targetState = m_pendingGameState;
				m_prevClick = click;
				return result;
			}
		}

		// Back button
		float backY = 0.14f - (int)m_templates.size() * 0.18f;
		if (nx >= btnX && nx <= btnX + btnW && ny >= backY && ny <= backY + btnH) {
			m_cooldown = 0.3f;
			result.type = MenuAction::BackToMenu;
			m_prevClick = click;
			return result;
		}
	}
	m_prevClick = click;

	// ---- Draw ----
	glClearColor(0.15f, 0.18f, 0.22f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	const char* modeLabel = (m_pendingGameState == GameState::CREATIVE) ? "Creative" : "Survival";
	char title[64];
	snprintf(title, sizeof(title), "Select World - %s", modeLabel);
	float titleW = strlen(title) * 0.018f * 1.5f;
	text.drawText(title, -titleW * 0.5f, 0.45f, 1.5f, {1, 1, 1, 1}, aspect);

	float btnW = 0.56f, btnH = 0.12f, btnX = -0.28f;
	for (int i = 0; i < (int)m_templates.size(); i++) {
		float btnY = 0.14f - i * 0.18f;
		text.drawRect(btnX, btnY, btnW, btnH, {0.25f, 0.30f, 0.35f, 0.9f});

		std::string label = m_templates[i]->name();
		float tw = label.size() * 0.018f * 1.2f;
		text.drawText(label.c_str(), btnX + (btnW - tw) * 0.5f, btnY + 0.055f, 1.2f, {1, 1, 1, 1}, aspect);

		std::string desc = m_templates[i]->description();
		float dw = desc.size() * 0.018f * 0.7f;
		text.drawText(desc.c_str(), btnX + (btnW - dw) * 0.5f, btnY + 0.015f, 0.7f, {0.6f, 0.6f, 0.6f, 1}, aspect);
	}

	// Back button
	float backY = 0.14f - (int)m_templates.size() * 0.18f;
	text.drawRect(btnX, backY, btnW, btnH, {0.25f, 0.30f, 0.35f, 0.9f});
	text.drawText("Back", btnX + (btnW - 0.07f) * 0.5f, backY + 0.03f, 1.2f, {1, 1, 1, 1}, aspect);

	window.swapBuffers();
	return result; // None
}

// -----------------------------------------------------------------
// CONTROLS SCREEN
// -----------------------------------------------------------------
MenuAction MenuSystem::updateControls(float dt, Window& window, TextRenderer& text,
                                      ControlManager& controls, float aspect)
{
	(void)dt;

	glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	MenuAction result;

	// Escape / MenuBack → back to main menu
	if (controls.pressed(Action::MenuBack)) {
		m_cooldown = 0.5f;
		result.type = MenuAction::BackToMenu;
		return result;
	}

	// Back button hit-test
	float backX = -0.15f, backY = -0.85f, backW = 0.30f, backH = 0.10f;
	bool click = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if (click && !m_prevClick) {
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		float nx = (float)(mx / window.width()) * 2 - 1;
		float ny = 1 - (float)(my / window.height()) * 2;
		if (nx >= backX && nx <= backX + backW && ny >= backY && ny <= backY + backH) {
			m_cooldown = 0.3f;
			result.type = MenuAction::BackToMenu;
			m_prevClick = click;
			return result;
		}
	}
	m_prevClick = click;

	// ---- Draw ----
	glClearColor(0.12f, 0.14f, 0.18f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	text.drawTitle("Controls", -0.16f, 0.78f, 2.5f, {0.95f, 0.95f, 1.0f, 1}, aspect);
	text.drawText("Key Bindings", -0.11f, 0.67f, 0.8f, {0.5f, 0.5f, 0.6f, 1}, aspect);

	// Column headers
	float colAction = -0.70f;
	float colKey = 0.20f;
	float rowY = 0.58f;
	float rowH = 0.055f;

	text.drawText("Action", colAction, rowY, 0.9f, {0.8f, 0.8f, 0.5f, 1}, aspect);
	text.drawText("Key", colKey, rowY, 0.9f, {0.8f, 0.8f, 0.5f, 1}, aspect);
	rowY -= rowH * 0.7f;

	// Separator line
	text.drawRect(colAction, rowY + 0.01f, 1.20f, 0.003f, {0.4f, 0.4f, 0.4f, 0.6f});
	rowY -= rowH * 0.5f;

	// List all bindings
	auto& bindings = controls.bindings();
	for (size_t i = 0; i < bindings.size() && rowY > -0.78f; i++) {
		auto& b = bindings[i];
		glm::vec4 textColor = {0.85f, 0.85f, 0.85f, 1.0f};

		// Alternate row shading
		if (i % 2 == 0)
			text.drawRect(colAction - 0.02f, rowY - 0.005f, 1.24f, rowH - 0.005f,
				{0.18f, 0.20f, 0.24f, 0.4f});

		text.drawText(b.displayName.c_str(), colAction, rowY, 0.75f, textColor, aspect);
		text.drawText(b.keyName.c_str(), colKey, rowY, 0.75f, {0.5f, 0.8f, 1.0f, 1}, aspect);
		rowY -= rowH;
	}

	// Scroll wheel note
	rowY -= rowH * 0.3f;
	text.drawText("Scroll Wheel: Hotbar select / Camera zoom",
		colAction, rowY, 0.6f, {0.5f, 0.5f, 0.5f, 1}, aspect);

	// Back button
	text.drawRect(backX, backY, backW, backH, {0.25f, 0.30f, 0.35f, 0.9f});
	text.drawText("Back", backX + 0.08f, backY + 0.025f, 1.1f, {1, 1, 1, 1}, aspect);

	window.swapBuffers();
	return result; // None
}

// -----------------------------------------------------------------
// CHARACTER SELECT
// -----------------------------------------------------------------
MenuAction MenuSystem::updateCharacterSelect(float dt, Window& window, TextRenderer& text,
                                             ControlManager& controls, ModelRenderer& modelRenderer,
                                             float aspect, float globalTime)
{
	(void)dt;
	glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

	MenuAction result;

	if (controls.pressed(Action::MenuBack)) {
		m_cooldown = 0.3f;
		result.type = MenuAction::BackToMenu;
		return result;
	}

	if (!m_characters || m_characters->count() == 0 || !m_faces || m_faces->count() == 0) {
		result.type = MenuAction::BackToMenu;
		return result;
	}

	int charCount = m_characters->count();
	float cardW = 0.34f;
	float cardH = 0.70f;  // shorter cards to make room for face row
	float gap = 0.04f;
	float totalW = charCount * cardW + (charCount - 1) * gap;
	float startX = -totalW / 2.0f;
	float cardY = -0.28f;

	// Mouse position
	double mx, my;
	glfwGetCursorPos(window.handle(), &mx, &my);
	float cnx = (float)(mx / window.width()) * 2 - 1;
	float cny = 1 - (float)(my / window.height()) * 2;

	bool click = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if (click && !m_prevClick) {
		// Character card clicks
		for (int i = 0; i < charCount; i++) {
			float cx = startX + i * (cardW + gap);
			if (cnx >= cx && cnx <= cx + cardW && cny >= cardY && cny <= cardY + cardH) {
				m_characters->select(i);
			}
		}

		// Face tile clicks
		if (m_faces) {
			int faceCount = m_faces->count();
			float faceW = 0.09f, faceGap = 0.02f;
			float faceTotalW = faceCount * faceW + (faceCount - 1) * faceGap;
			float faceStartX = -faceTotalW / 2.0f;
			float faceY = -0.36f - 0.12f;
			for (int fi = 0; fi < faceCount; fi++) {
				float fx = faceStartX + fi * (faceW + faceGap);
				if (cnx >= fx && cnx <= fx + faceW &&
				    cny >= faceY && cny <= faceY + faceW * aspect) {
					m_faces->select(fi);
				}
			}
		}

		// Back button
		float backX = -0.12f, backY = -0.92f, backW = 0.24f, backH = 0.08f;
		if (cnx >= backX && cnx <= backX + backW && cny >= backY && cny <= backY + backH) {
			m_cooldown = 0.3f;
			result.type = MenuAction::BackToMenu;
			m_prevClick = click;
			return result;
		}
	}
	m_prevClick = click;

	// ---- Draw ----
	glClearColor(0.12f, 0.14f, 0.18f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	text.drawTitle("Character", -0.18f, 0.80f, 2.2f, {0.95f, 0.95f, 1.0f, 1}, aspect);
	text.drawText("Select your character", -0.17f, 0.69f, 0.8f, {0.5f, 0.5f, 0.6f, 1}, aspect);

	for (int i = 0; i < charCount; i++) {
		float cx = startX + i * (cardW + gap);
		bool selected = (i == m_characters->selectedIndex());
		bool hovered = cnx >= cx && cnx <= cx + cardW &&
		               cny >= cardY && cny <= cardY + cardH;

		// Card background
		glm::vec4 cardColor = selected ? glm::vec4(0.25f, 0.30f, 0.45f, 0.85f)
		                     : hovered ? glm::vec4(0.22f, 0.25f, 0.32f, 0.7f)
		                               : glm::vec4(0.16f, 0.18f, 0.24f, 0.6f);
		text.drawRect(cx, cardY, cardW, cardH, cardColor);

		// Selection border
		if (selected) {
			float bw = 0.005f;
			text.drawRect(cx, cardY, cardW, bw, {0.5f, 0.7f, 1, 0.9f});
			text.drawRect(cx, cardY + cardH - bw, cardW, bw, {0.5f, 0.7f, 1, 0.9f});
			text.drawRect(cx, cardY, bw, cardH, {0.5f, 0.7f, 1, 0.9f});
			text.drawRect(cx + cardW - bw, cardY, bw, cardH, {0.5f, 0.7f, 1, 0.9f});
		}

		auto& cdef = m_characters->get(i);

		// 3D character preview: character body + selected face
		{
			BoxModel previewModel = m_characters->buildModel(i, m_faces->selected());

			// Gentle pendulum centered on facing the camera (-90°), ±30°
			float charYaw = -90.0f + std::sin(globalTime * 0.7f + i * 0.5f) * 30.0f;
			glm::vec3 camPos(0, 1.2f, -3.0f);
			glm::mat4 charView = glm::lookAt(camPos, glm::vec3(0, 0.9f, 0), glm::vec3(0, 1, 0));
			glm::mat4 charProj = glm::perspective(glm::radians(28.0f), cardW / (cardH * 0.5f), 0.1f, 50.0f);

			float previewCenterX = cx + cardW * 0.5f;
			float previewCenterY = cardY + cardH * 0.55f;
			glm::mat4 ndc(1.0f);
			ndc = glm::translate(ndc, glm::vec3(previewCenterX, previewCenterY, 0.0f));
			ndc = glm::scale(ndc, glm::vec3(cardW * 0.42f, cardH * 0.42f, 1.0f));

			glm::mat4 charVP = ndc * charProj * charView;
			AnimState anim = {globalTime * 2.0f, 2.0f, globalTime};
			modelRenderer.draw(previewModel, charVP, glm::vec3(0), charYaw, anim);
		}

		// Name
		float nameW = cdef.name.size() * 0.018f * 0.9f;
		text.drawText(cdef.name.c_str(),
			cx + (cardW - nameW) * 0.5f, cardY + 0.22f,
			0.9f, {1, 1, 1, 1}, aspect);

		// Stats (star ratings)
		{
			auto& st = cdef.stats;
			struct StatRow { const char* label; int val; };
			StatRow rows[] = {
				{"STR", st.strength}, {"STA", st.stamina},
				{"AGI", st.agility}, {"INT", st.intelligence},
			};
			float sy = cardY + 0.16f;
			for (auto& row : rows) {
				text.drawText(row.label, cx + 0.02f, sy, 0.4f, {0.6f, 0.6f, 0.5f, 1}, aspect);
				// Draw stars
				for (int s = 0; s < 5; s++) {
					glm::vec4 starCol = (s < row.val)
						? glm::vec4(1.0f, 0.85f, 0.15f, 1.0f)
						: glm::vec4(0.25f, 0.25f, 0.25f, 0.5f);
					text.drawText("*", cx + 0.10f + s * 0.025f, sy, 0.5f, starCol, aspect);
				}
				sy -= 0.035f;
			}
		}

		// Description
		text.drawText(cdef.description.c_str(),
			cx + 0.02f, cardY + 0.02f,
			0.40f, {0.5f, 0.5f, 0.5f, 1}, aspect);
	}

	// ---- Face selection row ----
	{
		float faceRowY = -0.36f;
		text.drawText("Face", -0.04f, faceRowY + 0.08f, 0.8f, {0.7f, 0.7f, 0.8f, 1}, aspect);

		int faceCount = m_faces->count();
		float faceW = 0.09f;
		float faceGap = 0.02f;
		float faceTotalW = faceCount * faceW + (faceCount - 1) * faceGap;
		float faceStartX = -faceTotalW / 2.0f;
		float faceY = faceRowY - 0.12f;

		for (int fi = 0; fi < faceCount; fi++) {
			float fx = faceStartX + fi * (faceW + faceGap);
			bool fSel = (fi == m_faces->selectedIndex());
			bool fHov = cnx >= fx && cnx <= fx + faceW &&
			            cny >= faceY && cny <= faceY + faceW * aspect;

			// Face tile background
			glm::vec4 fColor = fSel ? glm::vec4(0.30f, 0.35f, 0.50f, 0.9f)
			                  : fHov ? glm::vec4(0.22f, 0.25f, 0.35f, 0.7f)
			                         : glm::vec4(0.15f, 0.17f, 0.22f, 0.5f);
			text.drawRect(fx, faceY, faceW, faceW * aspect, fColor);

			if (fSel) {
				float bw = 0.003f;
				text.drawRect(fx, faceY, faceW, bw, {0.5f, 0.7f, 1, 0.8f});
				text.drawRect(fx, faceY + faceW * aspect - bw, faceW, bw, {0.5f, 0.7f, 1, 0.8f});
			}

			// Render face pixels as colored rects in the tile
			auto& fp = m_faces->get(fi);
			float px = faceW / 16.0f;
			float py = faceW * aspect / 16.0f;
			for (int row = 0; row < 16; row++) {
				for (int col = 0; col < 16; col++) {
					uint8_t idx = fp.pixels[row][col];
					if (idx == 0) continue;
					glm::vec4 pc = facePaletteColor(idx);
					text.drawRect(fx + col * px, faceY + (15 - row) * py, px, py, pc);
				}
			}

			// Face name under tile
			float nameW = fp.name.size() * 0.018f * 0.45f;
			text.drawText(fp.name.c_str(),
				fx + (faceW - nameW) * 0.5f, faceY - 0.03f,
				0.45f, {0.6f, 0.6f, 0.6f, 1}, aspect);
		}
	}

	// Back button
	float backX = -0.12f, backY = -0.92f, backW = 0.24f, backH = 0.08f;
	text.drawRect(backX, backY, backW, backH, {0.25f, 0.30f, 0.35f, 0.9f});
	text.drawText("Back", backX + 0.065f, backY + 0.02f, 1.0f, {1, 1, 1, 1}, aspect);

	window.swapBuffers();
	return result;
}

} // namespace aicraft

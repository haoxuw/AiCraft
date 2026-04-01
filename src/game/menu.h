#pragma once

#include "game/types.h"
#include "client/text.h"
#include "client/window.h"
#include "client/controls.h"
#include "client/model.h"
#include "server/world_template.h"
#include "shared/character.h"
#include "shared/face.h"

#include <memory>
#include <vector>

namespace aicraft {

class MenuSystem {
public:
	void init(const std::vector<std::shared_ptr<WorldTemplate>>& templates,
	          CharacterManager* characters, FaceLibrary* faces);

	MenuAction updateMainMenu(float dt, Window& window, TextRenderer& text,
	                          ControlManager& controls, float aspect,
	                          bool demoMode, float autoScreenTimer);

	MenuAction updateTemplateSelect(float dt, Window& window, TextRenderer& text,
	                                ControlManager& controls, float aspect);

	MenuAction updateControls(float dt, Window& window, TextRenderer& text,
	                          ControlManager& controls, float aspect);

	MenuAction updateCharacterSelect(float dt, Window& window, TextRenderer& text,
	                                 ControlManager& controls, ModelRenderer& modelRenderer,
	                                 float aspect, float globalTime);

	void setPendingGameState(GameState gs);
	GameState pendingGameState() const;
	void resetCooldown(float t = 0.5f);

private:
	bool m_prevClick = false;
	float m_cooldown = 0.5f;
	GameState m_pendingGameState = GameState::SURVIVAL;
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;
	CharacterManager* m_characters = nullptr;
	FaceLibrary* m_faces = nullptr;
};

} // namespace aicraft

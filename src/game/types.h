#pragma once

namespace aicraft {

enum class GameState { MENU, TEMPLATE_SELECT, CREATIVE, SURVIVAL, CONTROLS, CHARACTER, ENTITY_INSPECT, CODE_EDITOR };

constexpr int HOTBAR_SIZE = 10; // keys 1-9, 0

struct MenuAction {
	enum Type { None, Quit, ShowTemplateSelect, EnterGame, ShowControls, ShowCharacter, BackToMenu };
	Type type = None;
	int templateIndex = 0;
	GameState targetState = GameState::MENU;
};

} // namespace aicraft

#pragma once

namespace aicraft {

enum class GameState { MENU, TEMPLATE_SELECT, CREATIVE, SURVIVAL, CONTROLS, CHARACTER };

constexpr int HOTBAR_SIZE = 9;

struct MenuAction {
	enum Type { None, Quit, ShowTemplateSelect, EnterGame, ShowControls, ShowCharacter, BackToMenu };
	Type type = None;
	int templateIndex = 0;
	GameState targetState = GameState::MENU;
};

} // namespace aicraft

#pragma once
#include <string>

namespace aicraft {

enum class GameState {
	MENU,
	SERVER_BROWSER,   // show detected servers / start local
	TEMPLATE_SELECT,
	CREATIVE,
	SURVIVAL,
	CONTROLS,
	CHARACTER,
	ENTITY_INSPECT,
	CODE_EDITOR
};

constexpr int HOTBAR_SIZE = 10; // keys 1-9, 0

struct MenuAction {
	enum Type {
		None, Quit, BackToMenu,
		StartGame,         // → probe servers, show browser or auto-start
		ShowControls,
		ShowCharacter,
		EnterGame,         // → actually join a server/create world
		JoinServer,        // → connect to a specific server
	};
	Type type = None;
	int templateIndex = 0;
	GameState targetState = GameState::SURVIVAL;
	// For JoinServer
	std::string serverHost;
	int serverPort = 7777;
};

} // namespace aicraft

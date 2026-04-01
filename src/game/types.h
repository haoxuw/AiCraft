#pragma once
#include <string>

namespace agentworld {

enum class GameState {
	MENU,
	SERVER_BROWSER,   // show detected servers / start local
	TEMPLATE_SELECT,
	ADMIN,        // admin mode (F12 to toggle)
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

} // namespace agentworld

#pragma once
#include <string>
#include "server/world_gen_config.h"

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
	CODE_EDITOR,
	PAUSED
};

constexpr int HOTBAR_SIZE = 10; // keys 1-9, 0

struct MenuAction {
	enum Type {
		None, Quit, BackToMenu,
		StartGame,         // → probe servers, show browser or auto-start
		ShowControls,
		ShowCharacter,
		EnterGame,         // → create new world from template
		JoinServer,        // → connect to a specific server
		ResumeGame,        // → return to running game
		LoadWorld,         // → load a saved world
		DeleteWorld,       // → delete a saved world
	};
	Type type = None;
	int templateIndex = 0;
	int seed = 0;              // 0 = random
	GameState targetState = GameState::SURVIVAL;
	std::string worldName;     // for LoadWorld/EnterGame
	std::string worldPath;     // save directory path
	std::string serverHost;
	int serverPort = 7777;
	WorldGenConfig worldGenConfig;
};

} // namespace agentworld

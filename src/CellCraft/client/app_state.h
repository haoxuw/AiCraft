#pragma once

namespace civcraft::cellcraft {

enum class AppState {
	LOADING,
	MAIN_MENU,
	MONSTER_SELECT,
	DRAW_LAB,
	KID_STARTER,
	KID_LAB,
	KID_CELEBRATE,
	PLAYING,
	END_SCREEN,
};

// Top-level mode toggle on the main menu. Default is KID — kids are the
// higher-priority audience.
enum class GameMode {
	CLASSIC,
	KID,
};

} // namespace civcraft::cellcraft

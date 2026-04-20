#pragma once

// HandbookPanel — the in-game wiki. Browses every ArtifactRegistry entry
// (living, items, blocks, behaviors, effects, resources, worlds,
// structures, annotations, models) on the shared ScreenShell chrome.
//
// Keyboard model (matches Civilopedia muscle memory):
//   Left / Right or A / D → switch category
//   Up / Down or W / S    → switch entry within category
//   Esc                   → close (handled by the caller: menu returns to
//                           Main, in-game clears m_handbookOpen)
//
// For categories with 3D models (living, item, annotation, model) the
// shell's previewId is set each frame so game_vk_renderer_world.cpp
// injects the selected artifact into the scene, framed by the pinned
// camera in game_vk.cpp's menu block. Other categories leave previewId
// empty — the camera stays on its ambient orbit and the preview area
// shows either the game (in-game overlay) or the menu plaza.

#include <chrono>
#include <string>
#include <vector>

namespace civcraft::vk {

class Game;

class HandbookPanel {
public:
	// Reset cursor state — call when opening the panel so the user lands on
	// a deterministic first entry.
	void reset() {
		m_categoryCursor = 0;
		m_entryCursor    = 0;
		m_entryScroll    = 0;
	}

	// Called every frame while the panel is visible. Reads registry +
	// input + mutates shell.previewId on the Game directly. Defined in
	// handbook_panel.cpp where Game's full definition is in scope.
	void render(Game& g);

private:
	int m_categoryCursor = 0;
	int m_entryCursor    = 0;
	int m_entryScroll    = 0;   // first visible entry row

	std::vector<std::string> m_categoriesCache;
	std::string              m_titleBuf;   // storage for shell.title

	// Voice tab: debounces the Play Sample button so a double-click doesn't
	// queue two piper jobs that then play over each other.
	std::chrono::steady_clock::time_point m_lastVoiceSampleTime{};
};

} // namespace civcraft::vk

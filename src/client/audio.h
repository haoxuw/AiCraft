#pragma once

/**
 * AudioManager — lightweight spatial audio system using miniaudio.
 *
 * Loads .ogg/.wav files from the resources/sounds/ directory tree.
 * Supports grouped sounds (pick a random variant from a group).
 * Provides positional audio with distance-based attenuation.
 *
 * Sound groups map to the Sound:: constants in constants.h:
 *   dig_stone  → resources/sounds/blocks/impactMining_*.ogg
 *   dig_dirt   → resources/sounds/blocks/impactSoft_medium_*.ogg
 *   dig_wood   → resources/sounds/blocks/impactWood_medium_*.ogg
 *   step_grass → resources/sounds/footsteps/footstep_grass_*.ogg
 *   etc.
 */

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <set>
#include <cstdio>
#include <filesystem>

// Forward-declare miniaudio types to avoid including the huge header here
struct ma_engine;

namespace agentworld {

class AudioManager {
public:
	AudioManager() = default;
	~AudioManager();

	// Initialize the audio engine. Call once at startup.
	bool init();

	// Shutdown and free all resources.
	void shutdown();

	// Load all sounds from a directory tree (e.g. "resources/sounds").
	// Discovers files and registers them into named groups.
	void loadSoundsFrom(const std::string& basePath);

	// Play a sound group by name at a world position.
	// Picks a random variant from the group. Volume based on distance to listener.
	void play(const std::string& group, glm::vec3 worldPos, float volume = 1.0f);

	// Play a sound group at the listener position (non-positional, e.g. UI).
	void play(const std::string& group, float volume = 1.0f);

	// Play a specific file by path (for sound preview in handbook).
	void playFile(const std::string& path, float volume = 1.0f);

	// Play a procedurally-generated blip/bubble (no file needed).
	// Uses pure sine wave with decay — tests audio pipeline without codecs.
	// pitch: 0.5 = low bubble, 1.0 = normal blip, 2.0 = high chirp
	void playBlip(float pitch = 1.0f, float volume = 0.5f);

	// Background music control
	void startMusic();       // start playing random music tracks
	void stopMusic();        // stop background music
	void nextTrack();        // skip to next track (Ctrl+>)
	void prevTrack();        // go back to previous track (Ctrl+<)
	void skipAndDisable();   // skip current track AND mark it disabled for future
	void updateMusic();      // call each frame to handle track transitions
	bool musicPlaying() const { return m_musicPlaying; }
	void setMusicVolume(float vol);
	float musicVolume() const { return m_musicVolume; }
	std::string currentTrackName() const;

	// Disabled tracks — tracks the user skipped and doesn't want to hear again
	bool isTrackDisabled(int index) const;
	void setTrackDisabled(int index, bool disabled);
	int trackCount() const { return (int)m_musicFiles.size(); }
	std::string trackName(int index) const;
	void saveDisabledTracks();   // persist to config/disabled_tracks.txt
	void loadDisabledTracks();   // load from config/disabled_tracks.txt

	// Update the listener position (call each frame from camera).
	void setListener(glm::vec3 pos, glm::vec3 forward);

	// Master volume [0..1]
	void setMasterVolume(float vol);
	float masterVolume() const { return m_masterVolume; }

	// Mute/unmute (global)
	void setMuted(bool muted) { m_muted = muted; }
	bool muted() const { return m_muted; }

	// Mute effects only (creature sounds, footsteps, digging, etc.)
	// Music continues playing when effects are muted.
	void setEffectsMuted(bool muted) { m_effectsMuted = muted; }
	bool effectsMuted() const { return m_effectsMuted; }

	// Query loaded sounds (for handbook resource browser)
	struct SoundInfo {
		std::string name;      // filename without extension
		std::string path;      // full file path
		std::string group;     // group name (e.g. "dig_stone")
		std::string category;  // subdirectory (e.g. "blocks")
	};
	const std::vector<SoundInfo>& allSounds() const { return m_allSounds; }

	// Get all group names
	std::vector<std::string> groupNames() const;

	// Get files in a specific group (for handbook preview)
	std::vector<std::string> filesInGroup(const std::string& group) const;

private:
	ma_engine* m_engine = nullptr;
	bool m_initialized = false;
	bool m_muted = false;
	bool m_effectsMuted = false;
	float m_masterVolume = 0.5f;

	// Sound group: multiple variants for the same logical sound
	struct SoundGroup {
		std::vector<std::string> files; // full paths to .ogg/.wav files
	};
	std::unordered_map<std::string, SoundGroup> m_groups;

	// All loaded sounds (for resource browser)
	std::vector<SoundInfo> m_allSounds;

	// Listener state
	glm::vec3 m_listenerPos = {0, 0, 0};

	// Generated blip WAV files (created at init, no external files needed)
	std::string m_blipPath;       // soft bubble
	std::string m_blipHighPath;   // higher pitch chirp
	std::string m_blipLowPath;    // lower pitch bubble
	void generateBlipWav(const std::string& path, float freqHz, float durationSec);

	// Background music
	std::vector<std::string> m_musicFiles;
	int m_musicIndex = -1;
	bool m_musicPlaying = false;
	float m_musicVolume = 0.50f;  // background music volume
	void* m_musicSound = nullptr; // ma_sound* (opaque to avoid header dep)
	std::set<int> m_disabledTracks; // indices of tracks user doesn't want
	void stopCurrentSound();        // internal: stop and free current ma_sound
	int advanceIndex(int direction); // internal: find next enabled track

	// RNG for picking random variants
	std::mt19937 m_rng{std::random_device{}()};

	// Register built-in sound group mappings
	void registerDefaultGroups(const std::string& basePath);
};

} // namespace agentworld

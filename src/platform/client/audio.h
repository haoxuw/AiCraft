#pragma once

// Spatial audio via miniaudio. Groups in resources/sounds/ map to Sound::
// constants in constants.h (e.g. dig_stone → blocks/impactMining_*).

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <random>
#include <set>
#include <cstdio>
#include <filesystem>

struct ma_engine;

namespace solarium {

class AudioManager {
public:
	AudioManager() = default;
	~AudioManager();

	bool init();
	void shutdown();

	// Walks directory tree and registers files into named groups.
	void loadSoundsFrom(const std::string& basePath);

	// Picks a random variant from group; volume attenuated by listener distance.
	void play(const std::string& group, glm::vec3 worldPos, float volume = 1.0f);

	// Non-positional (e.g. UI).
	void play(const std::string& group, float volume = 1.0f);

	void playFile(const std::string& path, float volume = 1.0f);

	// Procedurally-generated sine-decay blip. pitch: 0.5 low, 1.0 normal, 2.0 high.
	void playBlip(float pitch = 1.0f, float volume = 0.5f);

	void startMusic();
	void stopMusic();
	void nextTrack();
	void prevTrack();
	void skipAndDisable();   // skip current AND mark disabled for future
	void updateMusic();      // call each frame for track transitions
	bool musicPlaying() const { return m_musicPlaying; }
	void setMusicVolume(float vol);
	float musicVolume() const { return m_musicVolume; }
	std::string currentTrackName() const;

	bool isTrackDisabled(int index) const;
	void setTrackDisabled(int index, bool disabled);
	int trackCount() const { return (int)m_musicFiles.size(); }
	std::string trackName(int index) const;
	void saveDisabledTracks();
	void loadDisabledTracks();

	void setListener(glm::vec3 pos, glm::vec3 forward);

	void setMasterVolume(float vol);
	float masterVolume() const { return m_masterVolume; }

	void setMuted(bool muted) { m_muted = muted; }
	bool muted() const { return m_muted; }

	// Effects-only mute: music continues.
	void setEffectsMuted(bool muted) { m_effectsMuted = muted; }
	bool effectsMuted() const { return m_effectsMuted; }

	struct SoundInfo {
		std::string name;
		std::string path;
		std::string group;     // e.g. "dig_stone"
		std::string category;  // subdirectory
	};
	const std::vector<SoundInfo>& allSounds() const { return m_allSounds; }

	std::vector<std::string> groupNames() const;
	std::vector<std::string> filesInGroup(const std::string& group) const;

private:
	// Populate one sound group from files in basePath/subdir whose
	// stems start with `prefix`. Shared by the default-groups table
	// loop in registerDefaultGroups(); kept private because callers
	// should go through that table, not spray one-off groups in.
	void buildSoundGroup(const std::string& basePath,
	                      const std::string& group,
	                      const std::string& subdir,
	                      const std::string& prefix);

	ma_engine* m_engine = nullptr;
	bool m_initialized = false;
	bool m_muted = false;
	bool m_effectsMuted = false;
	float m_masterVolume = 0.5f;

	struct SoundGroup {
		std::vector<std::string> files;
	};
	std::unordered_map<std::string, SoundGroup> m_groups;

	std::vector<SoundInfo> m_allSounds;

	glm::vec3 m_listenerPos = {0, 0, 0};

	std::string m_blipPath;
	std::string m_blipHighPath;
	std::string m_blipLowPath;
	void generateBlipWav(const std::string& path, float freqHz, float durationSec);

	std::vector<std::string> m_musicFiles;
	int m_musicIndex = -1;
	bool m_musicPlaying = false;
	float m_musicVolume = 0.50f;
	void* m_musicSound = nullptr; // ma_sound* — opaque to avoid header dep
	std::set<int> m_disabledTracks;
	void stopCurrentSound();
	int advanceIndex(int direction);

	std::mt19937 m_rng{std::random_device{}()};

	void registerDefaultGroups(const std::string& basePath);
};

} // namespace solarium

// Include audio.h first (pulls in GLM) before stb_vorbis which has conflicting names
#include "client/audio.h"

// stb_vorbis: include header-only first so miniaudio sees the declarations,
// then miniaudio implementation, then stb_vorbis implementation.
#define STB_VORBIS_HEADER_ONLY
#include "client/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "client/miniaudio.h"

// Now pull in the full stb_vorbis implementation
#undef STB_VORBIS_HEADER_ONLY
#include "client/stb_vorbis.c"
#include <algorithm>
#include <cmath>

namespace modcraft {

AudioManager::~AudioManager() {
	shutdown();
}

bool AudioManager::init() {
	if (m_initialized) return true;

	m_engine = new ma_engine;
	ma_engine_config config = ma_engine_config_init();
	config.listenerCount = 1;

	ma_result result = ma_engine_init(&config, m_engine);
	if (result != MA_SUCCESS) {
		printf("[Audio] Failed to initialize audio engine (error %d)\n", result);
		delete m_engine;
		m_engine = nullptr;
		return false;
	}

	ma_engine_set_volume(m_engine, m_masterVolume);
	m_initialized = true;

	// Generate built-in blip sounds (pure sine waves, no external files)
	std::filesystem::create_directories("resources/sounds");
	m_blipPath = "resources/sounds/_blip.wav";
	m_blipHighPath = "resources/sounds/_blip_high.wav";
	m_blipLowPath = "resources/sounds/_blip_low.wav";
	generateBlipWav(m_blipPath, 880.0f, 0.08f);       // A5 — soft blip
	generateBlipWav(m_blipHighPath, 1320.0f, 0.06f);   // E6 — chirpy
	generateBlipWav(m_blipLowPath, 523.0f, 0.12f);     // C5 — gentle bubble

	printf("[Audio] Initialized (engine OK, blips generated)\n");

	// Quick self-test: try playing the blip to verify pipeline
	result = ma_engine_play_sound(m_engine, m_blipPath.c_str(), nullptr);
	if (result != MA_SUCCESS) {
		printf("[Audio] WARNING: blip playback failed (error %d) — audio may not work\n", result);
	} else {
		printf("[Audio] Self-test blip played OK\n");
	}

	return true;
}

void AudioManager::shutdown() {
	if (m_engine) {
		ma_engine_uninit(m_engine);
		delete m_engine;
		m_engine = nullptr;
	}
	m_initialized = false;
	m_groups.clear();
	m_allSounds.clear();
}

void AudioManager::loadSoundsFrom(const std::string& basePath) {
	if (!std::filesystem::exists(basePath)) {
		printf("[Audio] Sound directory not found: %s\n", basePath.c_str());
		return;
	}

	int fileCount = 0;

	// Walk the directory tree and register every .ogg/.wav file
	for (auto& entry : std::filesystem::recursive_directory_iterator(basePath)) {
		if (!entry.is_regular_file()) continue;
		auto ext = entry.path().extension().string();
		if (ext != ".ogg" && ext != ".wav") continue;

		std::string fullPath = entry.path().string();
		std::string filename = entry.path().stem().string();

		// Determine category from parent directory name
		std::string category = entry.path().parent_path().filename().string();

		// Add to "all sounds" list for resource browser
		SoundInfo info;
		info.name = filename;
		info.path = fullPath;
		info.category = category;
		info.group = ""; // assigned below by registerDefaultGroups
		m_allSounds.push_back(info);
		fileCount++;
	}

	// Register the default group mappings
	registerDefaultGroups(basePath);

	// Discover music files (in music/ subdirectory)
	std::string musicDir = basePath + "/music";
	if (std::filesystem::exists(musicDir)) {
		for (auto& entry : std::filesystem::directory_iterator(musicDir)) {
			if (!entry.is_regular_file()) continue;
			auto ext = entry.path().extension().string();
			if (ext == ".mp3" || ext == ".ogg" || ext == ".wav") {
				m_musicFiles.push_back(entry.path().string());
			}
		}
		std::sort(m_musicFiles.begin(), m_musicFiles.end());
		loadDisabledTracks();
		printf("[Audio] Found %zu music tracks (%zu disabled)\n",
		       m_musicFiles.size(), m_disabledTracks.size());
	}

	printf("[Audio] Loaded %d sound files, %zu groups from %s\n",
	       fileCount, m_groups.size(), basePath.c_str());
}

void AudioManager::registerDefaultGroups(const std::string& basePath) {
	// Helper: register a group from files matching a prefix in a subdirectory
	auto addGroup = [&](const std::string& groupName, const std::string& subdir,
	                     const std::string& prefix) {
		std::string dir = basePath + "/" + subdir;
		if (!std::filesystem::exists(dir)) return;

		SoundGroup group;
		for (auto& entry : std::filesystem::directory_iterator(dir)) {
			if (!entry.is_regular_file()) continue;
			std::string stem = entry.path().stem().string();
			if (stem.find(prefix) == 0) {
				group.files.push_back(entry.path().string());
			}
		}
		if (!group.files.empty()) {
			m_groups[groupName] = std::move(group);
			// Tag the SoundInfo entries with their group
			for (auto& si : m_allSounds) {
				if (si.category == subdir && si.name.find(prefix) == 0) {
					si.group = groupName;
				}
			}
		}
	};

	// Block dig sounds — map to Sound:: constants
	addGroup("dig_stone",  "blocks", "impactMining");
	addGroup("dig_dirt",   "blocks", "impactSoft_medium");
	addGroup("dig_sand",   "blocks", "impactSoft_heavy");
	addGroup("dig_wood",   "blocks", "impactWood_medium");
	addGroup("dig_leaves", "blocks", "impactSoft_medium"); // reuse soft for leaves
	addGroup("dig_snow",   "blocks", "impactSoft_heavy");  // reuse soft for snow
	addGroup("dig_glass",  "blocks", "impactGlass_medium");
	addGroup("dig_metal",  "blocks", "impactMetal_medium");

	// Block place sounds (lighter variants)
	addGroup("place_stone", "blocks", "impactGeneric_light");
	addGroup("place_wood",  "blocks", "impactWood_light");
	addGroup("place_soft",  "blocks", "impactSoft_heavy");

	// Footstep sounds
	addGroup("step_stone", "footsteps", "footstep_concrete");
	addGroup("step_dirt",  "footsteps", "footstep0");       // generic footsteps
	addGroup("step_grass", "footsteps", "footstep_grass");
	addGroup("step_sand",  "footsteps", "footstep0");       // generic
	addGroup("step_wood",  "footsteps", "footstep_wood");
	addGroup("step_snow",  "footsteps", "footstep_snow");

	// Combat sounds
	addGroup("hit_punch",  "combat", "impactPunch_medium");
	addGroup("hit_sword",  "combat", "knifeSlice");
	addGroup("hit_shield", "combat", "impactPlate_light");

	// Item sounds
	addGroup("item_pickup",  "items",  "handleCoins");
	addGroup("item_equip",   "rpg",    "Cloth_");       // cloth equip (rpg/Cloth_01-07.wav)
	addGroup("item_equip_metal", "rpg", "chain_");      // metal equip (rpg/chain_01-03.ogg)
	addGroup("item_consume", "rpg",    "Drink_");       // eat/drink (rpg/Drink_01-06.wav)
	addGroup("item_book",    "items",  "bookFlip");

	// Creature sounds — real recordings (CC0 + CC-BY 3.0 with attribution)
	addGroup("creature_pig",     "creatures", "pig_idle");     // real pig oinks (Vinrax, CC-BY)
	addGroup("creature_chicken", "creatures", "chicken");      // real chicken cluck (IMadeIt, CC-BY)
	addGroup("creature_dog",     "creatures", "Dog Bark");     // real dog barks (pauliuw, CC0)
	addGroup("creature_cat",     "creatures", "cat_");         // real cat meows (Kerzoven, CC0)
	addGroup("creature_sheep",   "creatures", "sheep");        // sheep baa (AntumDeluge, CC0)
	addGroup("creature_bird",    "creatures", "quail");        // gentle quail chirps (CC0)

	// Water / liquid sounds
	addGroup("water_splash",  "water", "splash");
	addGroup("water_bubble",  "water", "bubble");
	addGroup("water_slime",   "water", "slime");
	addGroup("water_mud",     "water", "mud");
	addGroup("water_swim",    "water", "swim");

	// Spell / effect sounds
	addGroup("spell_heal",      "spells", "healing");
	addGroup("spell_teleport",  "spells", "teleport");
	addGroup("spell_electric",  "spells", "electricspell");
	addGroup("spell_powerup",   "spells", "power_up");
	addGroup("spell_buff",      "spells", "synth_beep");

	// Door sounds
	addGroup("door_open",       "doors", "qubodup-DoorOpen");
	addGroup("door_close",      "doors", "qubodup-DoorClose");
	addGroup("door_lock",       "doors", "DoorLock");
	addGroup("door_locked",     "doors", "LockedDoorHandle");

	// Ambient / environment
	addGroup("ambient_wind",    "ambient", "wind");
	addGroup("ambient_fire",    "ambient", "fire");
	addGroup("ambient_birds",   "ambient", "birds");
	addGroup("ambient_rain",    "ambient", "rain");

	// Explosions (TNT)
	addGroup("explosion",       "explosions", "bang");

	// RPG / combat extras
	addGroup("sword_swing",     "rpg", "blade");
	addGroup("armor_equip",     "rpg", "chain");
	addGroup("rpg_coin",        "rpg", "coin");

	// UI sounds
	addGroup("ui_click",    "ui", "click");
	addGroup("ui_select",   "ui", "select");
	addGroup("ui_confirm",  "ui", "confirmation");
	addGroup("ui_open",     "ui", "open");
	addGroup("ui_close",    "ui", "close");
	addGroup("ui_scroll",   "ui", "scroll");
	addGroup("ui_switch",   "ui", "switch_00"); // only the clean switch variants
	addGroup("ui_error",    "ui", "error");
}

void AudioManager::play(const std::string& group, glm::vec3 worldPos, float volume) {
	if (!m_initialized || m_muted || m_effectsMuted) return;

	auto it = m_groups.find(group);
	if (it == m_groups.end() || it->second.files.empty()) return;

	// Pick a random variant
	auto& files = it->second.files;
	std::uniform_int_distribution<size_t> dist(0, files.size() - 1);
	const std::string& file = files[dist(m_rng)];

	// Distance attenuation (simple linear falloff)
	float dist3d = glm::length(worldPos - m_listenerPos);
	float maxDist = 32.0f;
	if (dist3d > maxDist) return; // too far, don't play

	float attenuation = 1.0f - (dist3d / maxDist);
	attenuation = std::clamp(attenuation, 0.0f, 1.0f);
	float finalVol = volume * attenuation * m_masterVolume;
	if (finalVol < 0.01f) return;

	// Fire and forget playback
	ma_result r = ma_engine_play_sound(m_engine, file.c_str(), nullptr);
	if (r != MA_SUCCESS) {
		// Only log first failure per group to avoid spam
		static std::unordered_map<std::string, bool> warned;
		if (!warned[group]) {
			printf("[Audio] play('%s') failed for '%s' (error %d)\n", group.c_str(), file.c_str(), r);
			warned[group] = true;
		}
	}
}

void AudioManager::play(const std::string& group, float volume) {
	play(group, m_listenerPos, volume);
}

void AudioManager::playFile(const std::string& path, float volume) {
	if (!m_initialized || m_muted || m_effectsMuted) return;
	ma_result r = ma_engine_play_sound(m_engine, path.c_str(), nullptr);
	if (r != MA_SUCCESS) {
		printf("[Audio] playFile failed for '%s' (error %d)\n", path.c_str(), r);
	}
}

void AudioManager::generateBlipWav(const std::string& path, float freqHz, float durationSec) {
	// Generate a tiny WAV file with a sine wave + exponential decay.
	// This is a soft "bubble" sound — friendly and pleasant.
	const int sampleRate = 44100;
	const int numSamples = (int)(sampleRate * durationSec);
	const int numChannels = 1;
	const int bitsPerSample = 16;
	const int byteRate = sampleRate * numChannels * bitsPerSample / 8;
	const int blockAlign = numChannels * bitsPerSample / 8;
	const int dataSize = numSamples * blockAlign;

	std::vector<int16_t> samples(numSamples);
	for (int i = 0; i < numSamples; i++) {
		float t = (float)i / sampleRate;
		// Sine wave with exponential decay + slight pitch drop for "bubble" feel
		float decay = std::exp(-t * 30.0f); // fast decay
		float freq = freqHz * (1.0f - t * 2.0f); // slight downward pitch bend
		if (freq < 100.0f) freq = 100.0f;
		float sample = std::sin(2.0f * 3.14159265f * freq * t) * decay;
		// Soft clamp
		sample = std::clamp(sample, -1.0f, 1.0f);
		samples[i] = (int16_t)(sample * 24000); // not full volume, keep it soft
	}

	// Write WAV file
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) {
		printf("[Audio] Failed to write blip WAV: %s\n", path.c_str());
		return;
	}

	// RIFF header
	int32_t fileSize = 36 + dataSize;
	fwrite("RIFF", 1, 4, f);
	fwrite(&fileSize, 4, 1, f);
	fwrite("WAVE", 1, 4, f);

	// fmt chunk
	fwrite("fmt ", 1, 4, f);
	int32_t fmtSize = 16;
	int16_t audioFormat = 1; // PCM
	int16_t nc = numChannels;
	int32_t sr = sampleRate;
	int32_t br = byteRate;
	int16_t ba = blockAlign;
	int16_t bps = bitsPerSample;
	fwrite(&fmtSize, 4, 1, f);
	fwrite(&audioFormat, 2, 1, f);
	fwrite(&nc, 2, 1, f);
	fwrite(&sr, 4, 1, f);
	fwrite(&br, 4, 1, f);
	fwrite(&ba, 2, 1, f);
	fwrite(&bps, 2, 1, f);

	// data chunk
	int32_t ds = dataSize;
	fwrite("data", 1, 4, f);
	fwrite(&ds, 4, 1, f);
	fwrite(samples.data(), 2, numSamples, f);
	fclose(f);
}

void AudioManager::playBlip(float pitch, float volume) {
	if (!m_initialized || m_muted || m_effectsMuted) return;
	const std::string* path = &m_blipPath;
	if (pitch > 1.5f) path = &m_blipHighPath;
	else if (pitch < 0.7f) path = &m_blipLowPath;
	ma_result r = ma_engine_play_sound(m_engine, path->c_str(), nullptr);
	if (r != MA_SUCCESS) {
		printf("[Audio] playBlip failed (error %d)\n", r);
	}
}

void AudioManager::setListener(glm::vec3 pos, glm::vec3 forward) {
	m_listenerPos = pos;
	if (m_engine) {
		ma_engine_listener_set_position(m_engine, 0, pos.x, pos.y, pos.z);
		ma_engine_listener_set_direction(m_engine, 0, forward.x, forward.y, forward.z);
	}
}

void AudioManager::setMasterVolume(float vol) {
	m_masterVolume = std::clamp(vol, 0.0f, 1.0f);
	if (m_engine) {
		ma_engine_set_volume(m_engine, m_masterVolume);
	}
}

std::vector<std::string> AudioManager::groupNames() const {
	std::vector<std::string> names;
	names.reserve(m_groups.size());
	for (auto& [name, _] : m_groups)
		names.push_back(name);
	std::sort(names.begin(), names.end());
	return names;
}

std::vector<std::string> AudioManager::filesInGroup(const std::string& group) const {
	auto it = m_groups.find(group);
	if (it == m_groups.end()) return {};
	return it->second.files;
}

void AudioManager::startMusic() {
	if (!m_initialized) {
		printf("[Music] Cannot start: audio not initialized\n");
		return;
	}
	if (m_musicFiles.empty()) {
		printf("[Music] Cannot start: no music files found\n");
		return;
	}
	m_musicPlaying = true;
	// Shuffle: pick a random starting index
	std::uniform_int_distribution<int> dist(0, (int)m_musicFiles.size() - 1);
	m_musicIndex = dist(m_rng);
	printf("[Music] Starting playback (%zu tracks available, master=%.2f, music=%.2f)\n",
	       m_musicFiles.size(), m_masterVolume, m_musicVolume);
	nextTrack();
}

void AudioManager::stopMusic() {
	m_musicPlaying = false;
	stopCurrentSound();
}

void AudioManager::stopCurrentSound() {
	if (m_musicSound) {
		ma_sound_stop((ma_sound*)m_musicSound);
		ma_sound_uninit((ma_sound*)m_musicSound);
		delete (ma_sound*)m_musicSound;
		m_musicSound = nullptr;
	}
}

int AudioManager::advanceIndex(int direction) {
	// Find the next enabled track in the given direction (+1 or -1).
	// Returns -1 if all tracks are disabled.
	int n = (int)m_musicFiles.size();
	for (int i = 0; i < n; i++) {
		int idx = ((m_musicIndex + direction * (i + 1)) % n + n) % n;
		if (m_disabledTracks.count(idx) == 0)
			return idx;
	}
	return -1; // all disabled
}

void AudioManager::nextTrack() {
	if (!m_initialized || m_musicFiles.empty()) return;
	stopCurrentSound();

	int idx = advanceIndex(+1);
	if (idx < 0) {
		printf("[Music] All tracks disabled\n");
		m_musicPlaying = false;
		return;
	}
	m_musicIndex = idx;

	const std::string& path = m_musicFiles[m_musicIndex];
	auto* sound = new ma_sound;
	ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
	ma_result r = ma_sound_init_from_file(m_engine, path.c_str(), flags, nullptr, nullptr, sound);
	if (r != MA_SUCCESS) {
		printf("[Audio] Failed to load music: %s (error %d)\n", path.c_str(), r);
		delete sound;
		return;
	}

	ma_sound_set_volume(sound, m_musicVolume);
	r = ma_sound_start(sound);
	if (r != MA_SUCCESS) {
		printf("[Audio] Failed to start music: %s (error %d)\n", path.c_str(), r);
		ma_sound_uninit(sound);
		delete sound;
		return;
	}
	m_musicSound = sound;

	std::string name = std::filesystem::path(path).stem().string();
	printf("[Music] Now playing: %s (vol=%.2f)\n", name.c_str(), m_musicVolume);
}

void AudioManager::prevTrack() {
	if (!m_initialized || m_musicFiles.empty()) return;
	stopCurrentSound();

	int idx = advanceIndex(-1);
	if (idx < 0) {
		printf("[Music] All tracks disabled\n");
		m_musicPlaying = false;
		return;
	}
	m_musicIndex = idx;

	const std::string& path = m_musicFiles[m_musicIndex];
	auto* sound = new ma_sound;
	ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
	ma_result r = ma_sound_init_from_file(m_engine, path.c_str(), flags, nullptr, nullptr, sound);
	if (r != MA_SUCCESS) {
		printf("[Audio] Failed to load music: %s (error %d)\n", path.c_str(), r);
		delete sound;
		return;
	}

	ma_sound_set_volume(sound, m_musicVolume);
	ma_sound_start(sound);
	m_musicSound = sound;

	std::string name = std::filesystem::path(path).stem().string();
	printf("[Music] Now playing: %s (vol=%.2f)\n", name.c_str(), m_musicVolume);
}

void AudioManager::skipAndDisable() {
	if (m_musicIndex >= 0 && m_musicIndex < (int)m_musicFiles.size()) {
		std::string name = std::filesystem::path(m_musicFiles[m_musicIndex]).stem().string();
		m_disabledTracks.insert(m_musicIndex);
		printf("[Music] Disabled: %s (%zu/%zu tracks disabled)\n",
		       name.c_str(), m_disabledTracks.size(), m_musicFiles.size());
		saveDisabledTracks();
	}
	nextTrack();
}

void AudioManager::updateMusic() {
	if (!m_musicPlaying || !m_musicSound) return;

	// Check if current track finished → play next
	if (ma_sound_at_end((ma_sound*)m_musicSound)) {
		nextTrack();
	}
}

bool AudioManager::isTrackDisabled(int index) const {
	return m_disabledTracks.count(index) > 0;
}

void AudioManager::setTrackDisabled(int index, bool disabled) {
	if (disabled)
		m_disabledTracks.insert(index);
	else
		m_disabledTracks.erase(index);
	saveDisabledTracks();
}

std::string AudioManager::trackName(int index) const {
	if (index < 0 || index >= (int)m_musicFiles.size()) return "";
	std::string name = std::filesystem::path(m_musicFiles[index]).stem().string();
	if (name.size() > 3 && name[2] == '_') name = name.substr(3);
	for (auto& c : name) if (c == '_') c = ' ';
	return name;
}

void AudioManager::saveDisabledTracks() {
	std::string path = "config/disabled_tracks.txt";
	std::filesystem::create_directories("config");
	FILE* f = fopen(path.c_str(), "w");
	if (!f) return;
	for (int idx : m_disabledTracks) {
		if (idx >= 0 && idx < (int)m_musicFiles.size()) {
			fprintf(f, "%s\n", std::filesystem::path(m_musicFiles[idx]).filename().string().c_str());
		}
	}
	fclose(f);
}

void AudioManager::loadDisabledTracks() {
	std::string path = "config/disabled_tracks.txt";
	FILE* f = fopen(path.c_str(), "r");
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		// Strip newline
		std::string name(line);
		while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
			name.pop_back();
		if (name.empty()) continue;
		// Find matching index
		for (int i = 0; i < (int)m_musicFiles.size(); i++) {
			if (std::filesystem::path(m_musicFiles[i]).filename().string() == name) {
				m_disabledTracks.insert(i);
				break;
			}
		}
	}
	fclose(f);
	if (!m_disabledTracks.empty())
		printf("[Music] %zu tracks disabled from config\n", m_disabledTracks.size());
}

void AudioManager::setMusicVolume(float vol) {
	m_musicVolume = std::clamp(vol, 0.0f, 1.0f);
	if (m_musicSound) {
		ma_sound_set_volume((ma_sound*)m_musicSound, m_musicVolume);
	}
}

std::string AudioManager::currentTrackName() const {
	if (m_musicIndex < 0 || m_musicIndex >= (int)m_musicFiles.size()) return "";
	std::string name = std::filesystem::path(m_musicFiles[m_musicIndex]).stem().string();
	// Strip leading number prefix like "01_"
	if (name.size() > 3 && name[2] == '_') name = name.substr(3);
	// Replace underscores with spaces
	for (auto& c : name) if (c == '_') c = ' ';
	return name;
}

} // namespace modcraft

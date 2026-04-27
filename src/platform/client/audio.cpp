// audio.h (pulls GLM) must precede stb_vorbis to avoid name conflicts.
#include "client/audio.h"

// stb_vorbis header-only → miniaudio impl → stb_vorbis impl (ordering matters).
#define STB_VORBIS_HEADER_ONLY
#include "client/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "client/miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "client/stb_vorbis.c"
#include <algorithm>
#include <cmath>

namespace solarium {

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

	std::filesystem::create_directories("resources/sounds");
	m_blipPath = "resources/sounds/_blip.wav";
	m_blipHighPath = "resources/sounds/_blip_high.wav";
	m_blipLowPath = "resources/sounds/_blip_low.wav";
	generateBlipWav(m_blipPath, 880.0f, 0.08f);
	generateBlipWav(m_blipHighPath, 1320.0f, 0.06f);
	generateBlipWav(m_blipLowPath, 523.0f, 0.12f);

	printf("[Audio] Initialized (engine OK, blips generated)\n");

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

	for (auto& entry : std::filesystem::recursive_directory_iterator(basePath)) {
		if (!entry.is_regular_file()) continue;
		auto ext = entry.path().extension().string();
		if (ext != ".ogg" && ext != ".wav") continue;

		std::string fullPath = entry.path().string();
		std::string filename = entry.path().stem().string();
		std::string category = entry.path().parent_path().filename().string();

		SoundInfo info;
		info.name = filename;
		info.path = fullPath;
		info.category = category;
		info.group = ""; // set by registerDefaultGroups
		m_allSounds.push_back(info);
		fileCount++;
	}

	registerDefaultGroups(basePath);

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

// Group specs: (group name, subdir under basePath, filename prefix that
// gates files into the group). Kept in a table so adding a new sound
// pack is one line, and so the group-build loop has one copy of the
// filesystem-walk logic. File-list order matches the old inline code
// so the same dig/place/step variants still win RNG ties.
namespace {
struct SoundGroupSpec {
	const char* group;
	const char* subdir;
	const char* prefix;
};

// CC0 + CC-BY 3.0 recordings; see resources/sounds/creatures/ATTRIBUTION.
constexpr SoundGroupSpec kDefaultSoundGroups[] = {
	// Block mining
	{"dig_stone",   "blocks", "impactMining"},
	{"dig_dirt",    "blocks", "impactSoft_medium"},
	{"dig_sand",    "blocks", "impactSoft_heavy"},
	{"dig_wood",    "blocks", "impactWood_medium"},
	{"dig_leaves",  "blocks", "impactSoft_medium"},
	{"dig_snow",    "blocks", "impactSoft_heavy"},
	{"dig_glass",   "blocks", "impactGlass_medium"},
	{"dig_metal",   "blocks", "impactMetal_medium"},
	// Block placement
	{"place_stone", "blocks", "impactGeneric_light"},
	{"place_wood",  "blocks", "impactWood_light"},
	{"place_soft",  "blocks", "impactSoft_heavy"},
	// Footsteps
	{"step_stone",  "footsteps", "footstep_concrete"},
	{"step_dirt",   "footsteps", "footstep0"},
	{"step_grass",  "footsteps", "footstep_grass"},
	{"step_sand",   "footsteps", "footstep0"},
	{"step_wood",   "footsteps", "footstep_wood"},
	{"step_snow",   "footsteps", "footstep_snow"},
	// Combat
	{"hit_punch",   "combat", "impactPunch_medium"},
	{"hit_sword",   "combat", "knifeSlice"},
	{"hit_shield",  "combat", "impactPlate_light"},
	// Items / equip / consumables
	{"item_pickup",      "items", "handleCoins"},
	{"item_equip",       "rpg",   "Cloth_"},
	{"item_equip_metal", "rpg",   "chain_"},
	{"item_consume",     "rpg",   "Drink_"},
	{"item_book",        "items", "bookFlip"},
	// Creatures
	{"creature_pig",     "creatures", "pig_idle"},
	{"creature_chicken", "creatures", "chicken"},
	{"creature_dog",     "creatures", "Dog Bark"},
	{"creature_cat",     "creatures", "cat_"},
	{"creature_sheep",   "creatures", "sheep"},
	{"creature_bird",    "creatures", "quail"},
	// Water
	{"water_splash",  "water", "splash"},
	{"water_bubble",  "water", "bubble"},
	{"water_slime",   "water", "slime"},
	{"water_mud",     "water", "mud"},
	{"water_swim",    "water", "swim"},
	// Spells
	{"spell_heal",      "spells", "healing"},
	{"spell_teleport",  "spells", "teleport"},
	{"spell_electric",  "spells", "electricspell"},
	{"spell_powerup",   "spells", "power_up"},
	{"spell_buff",      "spells", "synth_beep"},
	// Doors
	{"door_open",       "doors", "qubodup-DoorOpen"},
	{"door_close",      "doors", "qubodup-DoorClose"},
	{"door_lock",       "doors", "DoorLock"},
	{"door_locked",     "doors", "LockedDoorHandle"},
	// Ambient / weather
	{"ambient_wind",    "ambient", "wind"},
	{"ambient_fire",    "ambient", "fire"},
	{"ambient_birds",   "ambient", "birds"},
	{"ambient_rain",    "ambient", "rain"},
	// Explosions
	{"explosion",       "explosions", "bang"},
	// RPG weapons / currency
	{"sword_swing",     "rpg", "blade"},
	{"armor_equip",     "rpg", "chain"},
	{"rpg_coin",        "rpg", "coin"},
	// UI — only the clean "switch_00" variant family.
	{"ui_click",        "ui", "click"},
	{"ui_select",       "ui", "select"},
	{"ui_confirm",      "ui", "confirmation"},
	{"ui_open",         "ui", "open"},
	{"ui_close",        "ui", "close"},
	{"ui_scroll",       "ui", "scroll"},
	{"ui_switch",       "ui", "switch_00"},
	{"ui_error",        "ui", "error"},
};
}  // namespace

// Build one group from the files in basePath/subdir whose stems begin
// with `prefix`. Also retro-labels the matching AllSounds entries with
// the group name so the UI browser can group recordings by intent.
void AudioManager::buildSoundGroup(const std::string& basePath,
                                     const std::string& group,
                                     const std::string& subdir,
                                     const std::string& prefix) {
	std::string dir = basePath + "/" + subdir;
	if (!std::filesystem::exists(dir)) return;

	SoundGroup g;
	for (auto& entry : std::filesystem::directory_iterator(dir)) {
		if (!entry.is_regular_file()) continue;
		std::string stem = entry.path().stem().string();
		if (stem.rfind(prefix, 0) == 0)
			g.files.push_back(entry.path().string());
	}
	if (g.files.empty()) return;

	m_groups[group] = std::move(g);
	for (auto& si : m_allSounds) {
		if (si.category == subdir && si.name.rfind(prefix, 0) == 0)
			si.group = group;
	}
}

void AudioManager::registerDefaultGroups(const std::string& basePath) {
	for (const auto& spec : kDefaultSoundGroups)
		buildSoundGroup(basePath, spec.group, spec.subdir, spec.prefix);
}

void AudioManager::play(const std::string& group, glm::vec3 worldPos, float volume) {
	if (!m_initialized || m_muted || m_effectsMuted) return;
	// Footstep gate: groups named "step_*" are walking sounds. Defaults
	// to muted because "constant clop-clop" was the #1 complaint pre-launch.
	if (m_footstepsMuted && group.rfind("step_", 0) == 0) return;

	auto it = m_groups.find(group);
	if (it == m_groups.end() || it->second.files.empty()) return;

	auto& files = it->second.files;
	std::uniform_int_distribution<size_t> dist(0, files.size() - 1);
	const std::string& file = files[dist(m_rng)];

	float dist3d = glm::length(worldPos - m_listenerPos);
	float maxDist = 32.0f;
	if (dist3d > maxDist) return;

	float attenuation = 1.0f - (dist3d / maxDist);
	attenuation = std::clamp(attenuation, 0.0f, 1.0f);
	float finalVol = volume * attenuation * m_masterVolume;
	if (finalVol < 0.01f) return;

	ma_result r = ma_engine_play_sound(m_engine, file.c_str(), nullptr);
	if (r != MA_SUCCESS) {
		// Log only first failure per group to avoid spam.
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
	// Sine wave + exponential decay → soft bubble sound.
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
		float decay = std::exp(-t * 30.0f);
		float freq = freqHz * (1.0f - t * 2.0f);
		if (freq < 100.0f) freq = 100.0f;
		float sample = std::sin(2.0f * 3.14159265f * freq * t) * decay;
		sample = std::clamp(sample, -1.0f, 1.0f);
		samples[i] = (int16_t)(sample * 24000);
	}

	FILE* f = fopen(path.c_str(), "wb");
	if (!f) {
		printf("[Audio] Failed to write blip WAV: %s\n", path.c_str());
		return;
	}

	int32_t fileSize = 36 + dataSize;
	fwrite("RIFF", 1, 4, f);
	fwrite(&fileSize, 4, 1, f);
	fwrite("WAVE", 1, 4, f);

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
	// direction is +1 or -1; returns -1 if all tracks disabled.
	int n = (int)m_musicFiles.size();
	for (int i = 0; i < n; i++) {
		int idx = ((m_musicIndex + direction * (i + 1)) % n + n) % n;
		if (m_disabledTracks.count(idx) == 0)
			return idx;
	}
	return -1;
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
		std::string name(line);
		while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
			name.pop_back();
		if (name.empty()) continue;
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
	if (name.size() > 3 && name[2] == '_') name = name.substr(3); // strip "01_" prefix
	for (auto& c : name) if (c == '_') c = ' ';
	return name;
}

} // namespace solarium

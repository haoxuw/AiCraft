#define MINIAUDIO_IMPLEMENTATION
#include "client/miniaudio.h"
#include "client/audio.h"
#include <algorithm>
#include <cmath>

namespace agentworld {

AudioManager::~AudioManager() {
	shutdown();
}

bool AudioManager::init() {
	if (m_initialized) return true;

	m_engine = new ma_engine;
	ma_engine_config config = ma_engine_config_init();
	config.listenerCount = 1;

	if (ma_engine_init(&config, m_engine) != MA_SUCCESS) {
		printf("[Audio] Failed to initialize audio engine\n");
		delete m_engine;
		m_engine = nullptr;
		return false;
	}

	ma_engine_set_volume(m_engine, m_masterVolume);
	m_initialized = true;
	printf("[Audio] Initialized\n");
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
	addGroup("item_pickup", "items", "handleCoins");
	addGroup("item_equip",  "items", "leather");
	addGroup("item_book",   "items", "bookFlip");

	// Creature ambient sounds — friendly, soft sounds
	// Using light/generic impacts as placeholder chirps/grunts
	addGroup("creature_pig",     "blocks", "impactSoft_medium");  // soft grunt
	addGroup("creature_chicken", "blocks", "impactGeneric_light"); // light chirp
	addGroup("creature_dog",     "blocks", "impactPlank_medium");  // friendly bark placeholder
	addGroup("creature_cat",     "blocks", "impactSoft_heavy");    // soft purr placeholder

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
	if (!m_initialized || m_muted) return;

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
	ma_engine_play_sound(m_engine, file.c_str(), nullptr);
	// Note: miniaudio fire-and-forget doesn't support per-sound volume easily,
	// but the distance culling prevents distant sounds from cluttering.
	// For proper spatial audio, we'd use ma_sound objects — future improvement.
}

void AudioManager::play(const std::string& group, float volume) {
	play(group, m_listenerPos, volume);
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

} // namespace agentworld

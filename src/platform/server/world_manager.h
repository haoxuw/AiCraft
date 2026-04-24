#pragma once

// Scans saves/ for saved worlds; supports create + delete for the menu UI.

#include "server/world_save.h"
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

namespace civcraft {

struct WorldInfo {
	std::string name;
	std::string path;       // "saves/my_village"
	std::string templateName;
	int templateIndex = 0;
	int seed = 42;
	std::string gameMode;
	std::string lastPlayed;
};

class WorldManager {
public:
	void setSavesDir(const std::string& dir) { m_savesDir = dir; }

	void refresh() {
		namespace fs = std::filesystem;
		m_worlds.clear();

		if (!fs::exists(m_savesDir)) {
			fs::create_directories(m_savesDir);
			return;
		}

		for (auto& entry : fs::directory_iterator(m_savesDir)) {
			if (!entry.is_directory()) continue;
			std::string worldPath = entry.path().string();
			if (!fs::exists(worldPath + "/world.json")) continue;

			WorldMetadata meta = loadWorldMetadata(worldPath);
			if (meta.name.empty()) meta.name = entry.path().filename().string();

			WorldInfo info;
			info.name = meta.name;
			info.path = worldPath;
			info.templateName = meta.templateName;
			info.templateIndex = meta.templateIndex;
			info.seed = meta.seed;
			info.gameMode = meta.gameMode;
			info.lastPlayed = meta.lastPlayed;
			m_worlds.push_back(info);
		}

		std::sort(m_worlds.begin(), m_worlds.end(), [](const WorldInfo& a, const WorldInfo& b) {
			return a.lastPlayed > b.lastPlayed;
		});
	}

	const std::vector<WorldInfo>& worlds() const { return m_worlds; }

	std::string createWorld(const std::string& name, int seed, int templateIndex,
	                        const std::string& templateName) {
		namespace fs = std::filesystem;
		std::string dirName = name;
		for (auto& c : dirName) {
			if (!isalnum(c) && c != '_' && c != '-' && c != ' ') c = '_';
		}

		std::string path = m_savesDir + "/" + dirName;
		int suffix = 1;
		std::string basePath = path;
		while (fs::exists(path)) {
			path = basePath + "_" + std::to_string(suffix++);
		}

		fs::create_directories(path);

		std::ofstream f(path + "/world.json");
		if (f.is_open()) {
			time_t now = time(nullptr);
			char timeBuf[64];
			strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", localtime(&now));

			f << "{\n";
			f << "  \"name\": \"" << name << "\",\n";
			f << "  \"seed\": " << seed << ",\n";
			f << "  \"templateIndex\": " << templateIndex << ",\n";
			f << "  \"templateName\": \"" << templateName << "\",\n";
			f << "  \"gameMode\": \"survival\",\n";
			f << "  \"worldTime\": 0.25,\n";
			f << "  \"spawnPos\": [30, 5, 30],\n";
			f << "  \"lastPlayed\": \"" << timeBuf << "\",\n";
			f << "  \"version\": 1\n";
			f << "}\n";
		}

		return path;
	}

	bool deleteWorld(const std::string& path) {
		namespace fs = std::filesystem;
		if (!fs::exists(path)) return false;
		fs::remove_all(path);
		return true;
	}

private:
	std::string m_savesDir = "saves";
	std::vector<WorldInfo> m_worlds;
};

} // namespace civcraft

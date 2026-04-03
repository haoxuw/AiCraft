#pragma once

/**
 * Behavior persistence — save/load custom behaviors to disk.
 *
 * artifacts/behaviors/base/   — built-in defaults (read-only)
 * artifacts/behaviors/player/ — player-modified behaviors
 *
 * Behaviors are saved as .py files. Entity references them by filename.
 * In the future, this will be replaced by cloud storage.
 */

#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>

namespace agentworld {

class BehaviorStore {
public:
	void init(const std::string& basePath = "artifacts/behaviors") {
		m_basePath = basePath;
		m_initialized = true;
		std::filesystem::create_directories(m_basePath + "/base");
		std::filesystem::create_directories(m_basePath + "/player");
	}

	bool isInitialized() const { return m_initialized; }

	// Save a behavior to disk. Returns the filename.
	std::string save(const std::string& name, const std::string& code) {
		std::string path = m_basePath + "/player/" + name + ".py";
		std::ofstream f(path);
		if (!f.is_open()) {
			printf("[BehaviorStore] Failed to save: %s\n", path.c_str());
			return "";
		}
		f << code;
		printf("[BehaviorStore] Saved: %s (%zu bytes)\n", path.c_str(), code.size());
		return path;
	}

	// Load a behavior from disk. Checks player/ first, then base/.
	std::string load(const std::string& name) {
		// Try player-modified version first
		std::string playerPath = m_basePath + "/player/" + name + ".py";
		std::string content = readFile(playerPath);
		if (!content.empty()) return content;

		// Fall back to built-in default
		std::string basePath = m_basePath + "/base/" + name + ".py";
		return readFile(basePath);
	}

	// List all available behaviors
	std::vector<std::string> list() {
		std::vector<std::string> names;
		for (auto& dir : {"base", "player"}) {
			std::string path = m_basePath + "/" + dir;
			if (!std::filesystem::exists(path)) continue;
			for (auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.path().extension() == ".py") {
					names.push_back(entry.path().stem().string());
				}
			}
		}
		return names;
	}

	// Delete a player-modified behavior (can't delete base/)
	bool remove(const std::string& name) {
		std::string path = m_basePath + "/player/" + name + ".py";
		if (std::filesystem::remove(path)) {
			printf("[BehaviorStore] Deleted: %s\n", path.c_str());
			return true;
		}
		return false;
	}

private:
	std::string readFile(const std::string& path) {
		std::ifstream f(path);
		if (!f.is_open()) return "";
		std::ostringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	bool m_initialized = false;
	std::string m_basePath;
};

} // namespace agentworld

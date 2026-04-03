#pragma once

/**
 * ArtifactRegistry — loads all Python definitions from the artifact store.
 *
 * This is the single source of truth for ALL game content:
 *   - Creatures (animals, monsters, NPCs)
 *   - Characters (playable skins)
 *   - Items (weapons, tools, potions)
 *   - Effects (renamed from actions — heal, damage, teleport)
 *   - Blocks (terrain, structures)
 *   - Behaviors (AI scripts)
 *
 * All definitions are Python dicts loaded from artifacts/{category}/base/*.py
 * and artifacts/{category}/player/*.py. The registry is read-only at runtime
 * — the Handbook UI reads from it to display content.
 *
 * Two sections:
 *   - Built-in (base/) — ships with the game, read-only
 *   - Custom (player/) — player-created content, editable
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <random>

namespace agentworld {

struct ArtifactEntry {
	std::string id;           // "base:pig"
	std::string name;         // "Pig"
	std::string category;     // "creature", "item", "block", "behavior", "effect"
	std::string subcategory;  // "animal", "weapon", "terrain", etc.
	std::string description;
	std::string filePath;     // path to .py file
	bool isBuiltin;           // true = base/, false = player/

	// Raw Python source (loaded from file)
	std::string source;

	// Parsed fields (from Python dict)
	std::unordered_map<std::string, std::string> fields;
};

class ArtifactRegistry {
public:
	void loadAll(const std::string& basePath = "artifacts") {
		m_entries.clear();
		m_basePath = basePath;

		loadCategory("creatures", "creature");
		loadCategory("items", "item");
		loadCategory("blocks", "block");
		loadCategory("behaviors", "behavior");
		loadCategory("effects", "effect");
		loadCategory("characters", "character");
		loadCategory("resources", "resource");
		loadCategory("worlds", "world");

		printf("[ArtifactRegistry] Loaded %zu artifacts from %s\n",
		       m_entries.size(), basePath.c_str());

		// Validate: creatures, characters, and items must have a model file
		int warnings = 0;
		for (auto& e : m_entries) {
			if (e.category == "creature" || e.category == "character" || e.category == "item") {
				std::string key = e.name;
				for (auto& c : key) c = (char)std::tolower((unsigned char)c);
				std::string modelPath = basePath + "/models/base/" + key + ".py";
				std::string modelPathPlayer = basePath + "/models/player/" + key + ".py";
				if (!std::filesystem::exists(modelPath) && !std::filesystem::exists(modelPathPlayer)) {
					printf("[ArtifactRegistry] WARNING: %s '%s' has no model file (%s.py)\n",
						e.category.c_str(), e.name.c_str(), key.c_str());
					warnings++;
				}
			}
		}
		if (warnings > 0)
			printf("[ArtifactRegistry] %d artifact(s) missing model files!\n", warnings);
	}

	// Get all entries
	const std::vector<ArtifactEntry>& entries() const { return m_entries; }

	// Filter by category
	std::vector<const ArtifactEntry*> byCategory(const std::string& cat) const {
		std::vector<const ArtifactEntry*> result;
		for (auto& e : m_entries)
			if (e.category == cat) result.push_back(&e);
		return result;
	}

	// Filter by category + builtin/custom
	std::vector<const ArtifactEntry*> byCategory(const std::string& cat, bool builtin) const {
		std::vector<const ArtifactEntry*> result;
		for (auto& e : m_entries)
			if (e.category == cat && e.isBuiltin == builtin) result.push_back(&e);
		return result;
	}

	// Find by ID
	const ArtifactEntry* findById(const std::string& id) const {
		for (auto& e : m_entries)
			if (e.id == id) return &e;
		return nullptr;
	}

	// Count
	size_t count() const { return m_entries.size(); }
	size_t countByCategory(const std::string& cat) const {
		size_t n = 0;
		for (auto& e : m_entries)
			if (e.category == cat) n++;
		return n;
	}

	const std::string& basePath() const { return m_basePath; }
	const std::string& playerNS() const { return m_playerNS; }

	// Set the player namespace (e.g. "p_a3f1"). Generated once per client session.
	void setPlayerNamespace(const std::string& ns) { m_playerNS = ns; }

	// Generate a short random player namespace: "p_" + 4 hex chars
	static std::string generatePlayerNamespace() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dist(0, 0xFFFF);
		char buf[16];
		snprintf(buf, sizeof(buf), "p_%04x", dist(gen));
		return buf;
	}

	// Fork an entry to the player's local directory.
	// Returns the new entry's ID, or "" on failure.
	std::string forkEntry(const std::string& id) {
		printf("[ArtifactRegistry::forkEntry] Looking up '%s'\n", id.c_str());
		const ArtifactEntry* src = findById(id);
		if (!src) {
			printf("[ArtifactRegistry::forkEntry] Entry NOT FOUND in %zu entries\n", m_entries.size());
			for (auto& e : m_entries)
				printf("  - '%s' (%s)\n", e.id.c_str(), e.filePath.c_str());
			return "";
		}
		printf("[ArtifactRegistry::forkEntry] Found: category='%s', file='%s', source=%zuB\n",
			src->category.c_str(), src->filePath.c_str(), src->source.size());

		// category → directory name (creature→creatures, item→items, etc.)
		std::string dirName = src->category + "s";

		// Extract the base name from the original filename
		std::filesystem::path srcPath(src->filePath);
		std::string stem = srcPath.stem().string();

		// Destination: artifacts/{dirName}/player/{playerNS}_{stem}.py
		std::string destDir = m_basePath + "/" + dirName + "/player";
		std::filesystem::create_directories(destDir);
		std::string newStem = m_playerNS + "_" + stem;
		std::string destPath = destDir + "/" + newStem + ".py";

		// Don't overwrite if it already exists
		if (std::filesystem::exists(destPath)) {
			printf("[ArtifactRegistry] Fork already exists: %s\n", destPath.c_str());
			return m_playerNS + ":" + stem;
		}

		// Rewrite source: replace the original namespace in the id field
		std::string newSource = src->source;
		// Find "id": "something:name" and replace namespace
		std::string oldIdField = "\"" + src->id + "\"";
		std::string newId = m_playerNS + ":" + stem;
		std::string newIdField = "\"" + newId + "\"";
		auto pos = newSource.find(oldIdField);
		if (pos != std::string::npos)
			newSource.replace(pos, oldIdField.size(), newIdField);

		// Write the forked file
		std::ofstream out(destPath);
		if (!out.is_open()) {
			printf("[ArtifactRegistry] Failed to write fork: %s\n", destPath.c_str());
			return "";
		}
		out << newSource;
		out.close();

		printf("[ArtifactRegistry] Forked %s → %s (%s)\n", id.c_str(), destPath.c_str(), newId.c_str());

		// Also fork the model file if one exists (creature/character/item need models)
		if (src->category == "creature" || src->category == "character" || src->category == "item") {
			std::string modelSrc = m_basePath + "/models/base/" + stem + ".py";
			if (std::filesystem::exists(modelSrc)) {
				std::string modelDestDir = m_basePath + "/models/player";
				std::filesystem::create_directories(modelDestDir);
				std::string modelDest = modelDestDir + "/" + newStem + ".py";
				if (!std::filesystem::exists(modelDest)) {
					std::filesystem::copy_file(modelSrc, modelDest);
					printf("[ArtifactRegistry] Forked model %s → %s\n", modelSrc.c_str(), modelDest.c_str());
				}
			}
		}

		// Reload to pick up the new entry
		loadAll(m_basePath);

		return newId;
	}

private:
	void loadCategory(const std::string& dirName, const std::string& category) {
		for (const char* subdir : {"base", "player"}) {
			std::string path = m_basePath + "/" + dirName + "/" + subdir;
			if (!std::filesystem::exists(path)) continue;

			bool isBuiltin = (std::string(subdir) == "base");

			for (auto& entry : std::filesystem::directory_iterator(path)) {
				if (entry.path().extension() != ".py") continue;
				if (entry.path().filename().string()[0] == '_') continue; // skip __init__.py

				ArtifactEntry artifact;
				artifact.category = category;
				artifact.isBuiltin = isBuiltin;
				artifact.filePath = entry.path().string();

				// Read source
				std::ifstream f(artifact.filePath);
				if (!f.is_open()) continue;
				std::ostringstream ss;
				ss << f.rdbuf();
				artifact.source = ss.str();

				// Parse basic fields from Python source
				artifact.name = entry.path().stem().string();
				artifact.id = std::string(isBuiltin ? "base:" : "player:") + artifact.name;

				// Try to extract name/description from Python dict
				parseFields(artifact);

				m_entries.push_back(std::move(artifact));
			}
		}
	}

	void parseFields(ArtifactEntry& e) {
		// Parse Python dict fields: handles both "key": "string" and "key": number
		auto extract = [&](const std::string& key) -> std::string {
			std::string pattern = "\"" + key + "\"";
			auto pos = e.source.find(pattern);
			if (pos == std::string::npos) return "";

			// Find the colon after the key
			auto colon = e.source.find(':', pos + pattern.size());
			if (colon == std::string::npos) return "";

			// Skip whitespace after colon
			size_t valStart = colon + 1;
			while (valStart < e.source.size() && (e.source[valStart] == ' ' || e.source[valStart] == '\t'))
				valStart++;

			if (valStart >= e.source.size()) return "";

			// Check if value is a quoted string
			if (e.source[valStart] == '"') {
				auto end = e.source.find('"', valStart + 1);
				if (end == std::string::npos) return "";
				return e.source.substr(valStart + 1, end - valStart - 1);
			}

			// Otherwise, extract until comma, newline, or comment
			size_t valEnd = valStart;
			while (valEnd < e.source.size() && e.source[valEnd] != ',' &&
			       e.source[valEnd] != '\n' && e.source[valEnd] != '#' &&
			       e.source[valEnd] != '}')
				valEnd++;
			// Trim trailing whitespace
			while (valEnd > valStart && (e.source[valEnd-1] == ' ' || e.source[valEnd-1] == '\t'))
				valEnd--;
			if (valEnd <= valStart) return "";
			return e.source.substr(valStart, valEnd - valStart);
		};

		std::string name = extract("name");
		if (!name.empty()) e.name = name;

		std::string id = extract("id");
		if (!id.empty()) e.id = id;

		std::string desc = extract("description");
		if (!desc.empty()) e.description = desc;

		std::string subcat = extract("category");
		if (!subcat.empty()) e.subcategory = subcat;

		// Store parsed fields
		e.fields["name"] = e.name;
		e.fields["id"] = e.id;
		if (!desc.empty()) e.fields["description"] = desc;
		if (!subcat.empty()) e.fields["subcategory"] = subcat;

		// Extract numeric fields
		for (auto& key : {"max_hp", "walk_speed", "damage", "range", "cooldown", "hardness"}) {
			std::string val = extract(key);
			if (!val.empty()) e.fields[key] = val;
		}

		// Extract model reference
		std::string model = extract("model");
		if (!model.empty()) e.fields["model"] = model;

		// Extract behavior reference
		std::string behavior = extract("behavior");
		if (!behavior.empty()) e.fields["behavior"] = behavior;

		// Extract equip slot
		std::string equipSlot = extract("equip_slot");
		if (!equipSlot.empty()) e.fields["equip_slot"] = equipSlot;

		// Extract effect
		std::string effect = extract("effect");
		if (!effect.empty()) e.fields["effect"] = effect;

		// Extract on_use
		std::string onUse = extract("on_use");
		if (!onUse.empty()) e.fields["on_use"] = onUse;

		// Resource-specific fields
		for (auto& key : {"source", "license", "source_url", "file_count", "format", "groups", "status"}) {
			std::string val = extract(key);
			if (!val.empty()) e.fields[key] = val;
		}
	}

	std::string m_basePath;
	std::string m_playerNS = "player"; // default, overridden per-client
	std::vector<ArtifactEntry> m_entries;
};

} // namespace agentworld

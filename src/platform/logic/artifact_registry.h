#pragma once

// Rule 1: single source of truth for ALL game content (living/items/blocks/behaviors/effects).
// Loads Python dicts from artifacts/{cat}/base/*.py (read-only) + artifacts/{cat}/player/*.py (editable).

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <random>
#include <algorithm>

namespace civcraft {

struct ArtifactEntry {
	std::string id;           // e.g. "pig"
	std::string name;
	std::string category;     // "living", "item", "block", "behavior", "effect", ...
	std::string subcategory;  // living: "humanoid"|"animal"; items: "weapon", etc.
	std::string description;
	std::string filePath;
	bool isBuiltin;           // base/ vs player/

	std::string source;       // raw .py
	std::unordered_map<std::string, std::string> fields;
	std::vector<std::string> tags;
};

class ArtifactRegistry {
public:
	void loadAll(const std::string& basePath = "artifacts") {
		m_entries.clear();
		m_basePath = basePath;

		loadCategory("living", "living");
		loadCategory("items", "item");
		loadCategory("blocks", "block");
		loadCategory("behaviors", "behavior");
		loadCategory("effects", "effect");
		loadCategory("resources", "resource");
		loadCategory("worlds", "world");
		loadCategory("annotations", "annotation");

		printf("[ArtifactRegistry] Loaded %zu artifacts from %s\n",
		       m_entries.size(), basePath.c_str());

		// Living/items must have a model file. Use "model" field if present, else file stem.
		int warnings = 0;
		for (auto& e : m_entries) {
			if (e.category == "living" || e.category == "item" || e.category == "annotation") {
				std::string key;
				auto mit = e.fields.find("model");
				if (mit != e.fields.end() && !mit->second.empty()) {
					key = mit->second;
				} else {
					key = std::filesystem::path(e.filePath).stem().string();
				}
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

	const std::vector<ArtifactEntry>& entries() const { return m_entries; }

	std::vector<const ArtifactEntry*> byCategory(const std::string& cat) const {
		std::vector<const ArtifactEntry*> result;
		for (auto& e : m_entries)
			if (e.category == cat) result.push_back(&e);
		return result;
	}

	std::vector<const ArtifactEntry*> byCategory(const std::string& cat, bool builtin) const {
		std::vector<const ArtifactEntry*> result;
		for (auto& e : m_entries)
			if (e.category == cat && e.isBuiltin == builtin) result.push_back(&e);
		return result;
	}

	// Unique categories in load order (for dynamic UI tabs).
	std::vector<std::string> allCategories() const {
		std::vector<std::string> result;
		std::unordered_set<std::string> seen;
		for (auto& e : m_entries) {
			if (seen.insert(e.category).second)
				result.push_back(e.category);
		}
		return result;
	}

	const ArtifactEntry* findById(const std::string& id) const {
		for (auto& e : m_entries)
			if (e.id == id) return &e;
		return nullptr;
	}

	// Consumed by EntityManager::mergeArtifactTags() to propagate Python-declared tags.
	std::vector<std::pair<std::string, std::vector<std::string>>> livingTags() const {
		std::vector<std::pair<std::string, std::vector<std::string>>> result;
		for (auto& e : m_entries) {
			if (e.category == "living" && !e.tags.empty())
				result.push_back({e.id, e.tags});
		}
		return result;
	}

	size_t count() const { return m_entries.size(); }
	size_t countByCategory(const std::string& cat) const {
		size_t n = 0;
		for (auto& e : m_entries)
			if (e.category == cat) n++;
		return n;
	}

	const std::string& basePath() const { return m_basePath; }
	const std::string& playerNS() const { return m_playerNS; }

	// Per-client session namespace, e.g. "p_a3f1".
	void setPlayerNamespace(const std::string& ns) { m_playerNS = ns; }

	// "p_" + 4 hex chars.
	static std::string generatePlayerNamespace() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dist(0, 0xFFFF);
		char buf[16];
		snprintf(buf, sizeof(buf), "p_%04x", dist(gen));
		return buf;
	}

	// Fork entry into player's local dir. Returns new ID, or "" on failure.
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

		// category→dir: "living" is already-plural, others get "s".
		std::string dirName = (src->category == "living") ? "living" : src->category + "s";

		std::filesystem::path srcPath(src->filePath);
		std::string stem = srcPath.stem().string();

		std::string destDir = m_basePath + "/" + dirName + "/player";
		std::filesystem::create_directories(destDir);
		std::string newStem = m_playerNS + "_" + stem;
		std::string destPath = destDir + "/" + newStem + ".py";

		if (std::filesystem::exists(destPath)) {
			printf("[ArtifactRegistry] Fork already exists: %s\n", destPath.c_str());
			return m_playerNS + ":" + stem;
		}

		// Rewrite namespace in the "id" field.
		std::string newSource = src->source;
		std::string oldIdField = "\"" + src->id + "\"";
		std::string newId = m_playerNS + ":" + stem;
		std::string newIdField = "\"" + newId + "\"";
		auto pos = newSource.find(oldIdField);
		if (pos != std::string::npos)
			newSource.replace(pos, oldIdField.size(), newIdField);

		std::ofstream out(destPath);
		if (!out.is_open()) {
			printf("[ArtifactRegistry] Failed to write fork: %s\n", destPath.c_str());
			return "";
		}
		out << newSource;
		out.close();

		printf("[ArtifactRegistry] Forked %s → %s (%s)\n", id.c_str(), destPath.c_str(), newId.c_str());

		// Also fork model (living/items need one).
		if (src->category == "living" || src->category == "item") {
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

				std::ifstream f(artifact.filePath);
				if (!f.is_open()) continue;
				std::ostringstream ss;
				ss << f.rdbuf();
				artifact.source = ss.str();

				artifact.name = entry.path().stem().string();
				artifact.id = isBuiltin ? artifact.name : (std::string("player:") + artifact.name);

				parseFields(artifact);

				m_entries.push_back(std::move(artifact));
			}
		}
	}

	void parseFields(ArtifactEntry& e) {
		// Handles "key": "string" and "key": number. Tokenizer only — no variable refs.
		auto extract = [&](const std::string& key) -> std::string {
			std::string pattern = "\"" + key + "\"";
			auto pos = e.source.find(pattern);
			if (pos == std::string::npos) return "";

			auto colon = e.source.find(':', pos + pattern.size());
			if (colon == std::string::npos) return "";

			size_t valStart = colon + 1;
			while (valStart < e.source.size() && (e.source[valStart] == ' ' || e.source[valStart] == '\t'))
				valStart++;

			if (valStart >= e.source.size()) return "";

			if (e.source[valStart] == '"') {
				auto end = e.source.find('"', valStart + 1);
				if (end == std::string::npos) return "";
				return e.source.substr(valStart + 1, end - valStart - 1);
			}

			// Unquoted: until , \n # or }
			size_t valEnd = valStart;
			while (valEnd < e.source.size() && e.source[valEnd] != ',' &&
			       e.source[valEnd] != '\n' && e.source[valEnd] != '#' &&
			       e.source[valEnd] != '}')
				valEnd++;
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

		e.fields["name"] = e.name;
		e.fields["id"] = e.id;
		if (!desc.empty()) e.fields["description"] = desc;
		if (!subcat.empty()) e.fields["subcategory"] = subcat;

		for (auto& key : {"walk_speed", "damage", "range", "cooldown", "hardness"}) {
			std::string val = extract(key);
			if (!val.empty()) e.fields[key] = val;
		}

		std::string model = extract("model");
		if (!model.empty()) e.fields["model"] = model;

		// Annotation-specific
		std::string slot = extract("slot");
		if (!slot.empty()) e.fields["slot"] = slot;
		std::string spawnChance = extract("spawn_chance");
		if (!spawnChance.empty()) e.fields["spawn_chance"] = spawnChance;
		auto spawnOn = extractList(e.source, "spawn_on");
		if (!spawnOn.empty()) {
			std::string joined;
			for (size_t i = 0; i < spawnOn.size(); i++) {
				if (i > 0) joined += ",";
				joined += spawnOn[i];
			}
			e.fields["spawn_on"] = joined;
		}

		std::string behavior = extract("behavior");
		if (!behavior.empty()) e.fields["behavior"] = behavior;

		// Equip slot + item action hooks
		std::string equipSlot = extract("equip_slot");
		if (!equipSlot.empty()) e.fields["equip_slot"] = equipSlot;
		std::string onUse = extract("on_use");
		if (!onUse.empty() && onUse != "None") e.fields["on_use"] = onUse;
		std::string onEquip = extract("on_equip");
		if (!onEquip.empty() && onEquip != "None") e.fields["on_equip"] = onEquip;
		std::string onInteract = extract("on_interact");
		if (!onInteract.empty() && onInteract != "None") e.fields["on_interact"] = onInteract;

		std::string effect = extract("effect");
		if (!effect.empty()) e.fields["effect"] = effect;

		// Resource-specific
		for (auto& key : {"source", "license", "source_url", "file_count", "format", "groups", "status"}) {
			std::string val = extract(key);
			if (!val.empty()) e.fields[key] = val;
		}

		// Items/blocks: "color": [r,g,b] → stored as 3 scalars so consumers skip list parsing.
		auto colorVals = extractFloatList(e.source, "color");
		if (colorVals.size() == 3) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%g", colorVals[0]); e.fields["color_r"] = buf;
			snprintf(buf, sizeof(buf), "%g", colorVals[1]); e.fields["color_g"] = buf;
			snprintf(buf, sizeof(buf), "%g", colorVals[2]); e.fields["color_b"] = buf;
		}

		// Optional silhouette hint for non-cube held items.
		std::string heldShape = extract("held_shape");
		if (!heldShape.empty()) e.fields["held_shape"] = heldShape;

		e.tags = extractList(e.source, "tags");

		// Auto-tag subcategory so humanoid/animal lives in one place (not duplicated as feature tag).
		if ((e.subcategory == "humanoid" || e.subcategory == "animal") &&
		    std::find(e.tags.begin(), e.tags.end(), e.subcategory) == e.tags.end()) {
			e.tags.push_back(e.subcategory);
		}

		if (!e.tags.empty()) {
			std::string joined;
			for (size_t i = 0; i < e.tags.size(); i++) {
				if (i > 0) joined += ", ";
				joined += e.tags[i];
			}
			e.fields["tags"] = joined;
		}
	}

	// "key": [0.5, 1.0, ...] — int/float literals between [ and ].
	static std::vector<float> extractFloatList(const std::string& source, const std::string& key) {
		std::vector<float> result;
		std::string pattern = "\"" + key + "\"";
		auto pos = source.find(pattern);
		if (pos == std::string::npos) return result;
		auto bracket = source.find('[', pos + pattern.size());
		if (bracket == std::string::npos) return result;
		auto end = source.find(']', bracket);
		if (end == std::string::npos) return result;

		size_t cur = bracket + 1;
		while (cur < end) {
			while (cur < end && (source[cur] == ' ' || source[cur] == '\t' ||
			                     source[cur] == ',' || source[cur] == '\n' ||
			                     source[cur] == '\r'))
				cur++;
			if (cur >= end) break;
			size_t numEnd = cur;
			while (numEnd < end && source[numEnd] != ',' && source[numEnd] != ' ' &&
			       source[numEnd] != '\t' && source[numEnd] != '\n' &&
			       source[numEnd] != '\r')
				numEnd++;
			if (numEnd > cur) {
				try {
					result.push_back(std::stof(source.substr(cur, numEnd - cur)));
				} catch (...) {
					return {}; // non-numeric entry → skip list entirely
				}
			}
			cur = numEnd;
		}
		return result;
	}

	// "key": ["a", "b", ...]
	static std::vector<std::string> extractList(const std::string& source, const std::string& key) {
		std::vector<std::string> result;
		std::string pattern = "\"" + key + "\"";
		auto pos = source.find(pattern);
		if (pos == std::string::npos) return result;

		auto bracket = source.find('[', pos + pattern.size());
		if (bracket == std::string::npos) return result;

		auto end = source.find(']', bracket);
		if (end == std::string::npos) return result;

		size_t cur = bracket + 1;
		while (cur < end) {
			auto qStart = source.find('"', cur);
			if (qStart == std::string::npos || qStart >= end) break;
			auto qEnd = source.find('"', qStart + 1);
			if (qEnd == std::string::npos || qEnd >= end) break;
			result.push_back(source.substr(qStart + 1, qEnd - qStart - 1));
			cur = qEnd + 1;
		}
		return result;
	}

	std::string m_basePath;
	std::string m_playerNS = "player"; // overridden per-client
	std::vector<ArtifactEntry> m_entries;
};

} // namespace civcraft

#pragma once

// Rule 1: single source of truth for ALL game content (living/items/blocks/behaviors/effects).
// Loads Python dicts from artifacts/{cat}/base/*.py. User/mod content will be
// served from a database layer (not yet implemented) — there is no on-disk
// player/ tier.

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace civcraft {

struct ArtifactEntry {
	std::string id;           // e.g. "pig"
	std::string name;
	std::string category;     // "living", "item", "block", "behavior", "effect", ...
	std::string subcategory;  // living: "humanoid"|"animal"; items: "weapon", etc.
	std::string description;
	std::string filePath;

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
		loadCategory("structures", "structure");
		loadCategory("models", "model");

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
				if (!std::filesystem::exists(modelPath)) {
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

	// Per-living numeric overrides from Python. Fields left unset (NaN / empty)
	// mean "keep whatever registerAllBuiltins installed". Consumed by
	// EntityManager::applyLivingStats to keep C++ defaults data-driven.
	struct LivingStats {
		std::string id;
		std::string behavior;       // Python behavior module id (e.g. "wander", "woodcutter")
		float walk_speed = std::nanf("");
		float run_speed  = std::nanf("");
		float eye_height = std::nanf("");
		float gravity    = std::nanf("");
		bool  has_box = false;
		float box_min_x = 0, box_min_y = 0, box_min_z = 0;
		float box_max_x = 0, box_max_y = 0, box_max_z = 0;
	};
	std::vector<LivingStats> livingStats() const {
		std::vector<LivingStats> result;
		for (auto& e : m_entries) {
			if (e.category != "living") continue;
			LivingStats s;
			s.id = e.id;
			auto bIt = e.fields.find("behavior");
			if (bIt != e.fields.end()) s.behavior = bIt->second;
			auto asFloat = [&](const char* key, float& out) {
				auto it = e.fields.find(key);
				if (it != e.fields.end()) {
					try { out = std::stof(it->second); } catch (...) {}
				}
			};
			asFloat("walk_speed", s.walk_speed);
			asFloat("run_speed",  s.run_speed);
			asFloat("eye_height", s.eye_height);
			asFloat("gravity",    s.gravity);
			auto mnX = e.fields.find("collision_min_x");
			auto mxX = e.fields.find("collision_max_x");
			if (mnX != e.fields.end() && mxX != e.fields.end()) {
				try {
					s.box_min_x = std::stof(mnX->second);
					s.box_min_y = std::stof(e.fields.at("collision_min_y"));
					s.box_min_z = std::stof(e.fields.at("collision_min_z"));
					s.box_max_x = std::stof(mxX->second);
					s.box_max_y = std::stof(e.fields.at("collision_max_y"));
					s.box_max_z = std::stof(e.fields.at("collision_max_z"));
					s.has_box = true;
				} catch (...) {}
			}
			result.push_back(std::move(s));
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

private:
	void loadCategory(const std::string& dirName, const std::string& category) {
		std::string path = m_basePath + "/" + dirName + "/base";
		if (!std::filesystem::exists(path)) return;

		for (auto& entry : std::filesystem::directory_iterator(path)) {
			if (entry.path().extension() != ".py") continue;
			if (entry.path().filename().string()[0] == '_') continue; // skip __init__.py

			std::string filePath = entry.path().string();
			std::ifstream f(filePath);
			if (!f.is_open()) continue;
			std::ostringstream ss;
			ss << f.rdbuf();
			std::string source = ss.str();
			std::string stem = entry.path().stem().string();

			// Block files may hold a LIST of block dicts (blocks = [{...}, {...}])
			// rather than one dict per file. Emit one ArtifactEntry per dict so
			// the handbook can browse and crosslink each block independently.
			// Falls back to single-entry parse for files without the list shape.
			if (category == "block") {
				auto dicts = splitListDicts(source, "blocks");
				if (!dicts.empty()) {
					for (auto& sub : dicts) {
						ArtifactEntry a;
						a.category = category;
						a.filePath = filePath;
						a.source = sub;
						a.name = stem;   // fallback; parseFields will override from the dict
						a.id   = stem;
						parseFields(a);
						m_entries.push_back(std::move(a));
					}
					continue;
				}
			}

			ArtifactEntry artifact;
			artifact.category = category;
			artifact.filePath = filePath;
			artifact.source   = source;
			artifact.name     = stem;
			artifact.id       = stem;

			parseFields(artifact);

			m_entries.push_back(std::move(artifact));
		}
	}

	// "<listKey> = [ {...}, {...}, ... ]" — return each top-level {...} dict
	// as its own substring. Brace-balanced; skips over quoted strings so a
	// closing brace inside a string doesn't break depth tracking.
	static std::vector<std::string> splitListDicts(const std::string& source,
	                                               const std::string& listKey) {
		std::vector<std::string> result;
		auto kpos = source.find(listKey);
		if (kpos == std::string::npos) return result;
		auto eq = source.find('=', kpos + listKey.size());
		if (eq == std::string::npos) return result;
		auto lbr = source.find('[', eq);
		if (lbr == std::string::npos) return result;

		size_t pos = lbr + 1;
		while (pos < source.size()) {
			while (pos < source.size() &&
			       (source[pos] == ' ' || source[pos] == '\t' ||
			        source[pos] == '\n' || source[pos] == '\r' ||
			        source[pos] == ','))
				pos++;
			if (pos >= source.size() || source[pos] == ']') break;
			if (source[pos] != '{') { pos++; continue; }

			size_t start = pos;
			int depth = 1;
			pos++;
			while (pos < source.size() && depth > 0) {
				char c = source[pos];
				if (c == '"') {
					// Skip past string literal so braces inside strings don't count.
					pos++;
					while (pos < source.size() && source[pos] != '"') {
						if (source[pos] == '\\' && pos + 1 < source.size()) pos++;
						pos++;
					}
				} else if (c == '{') {
					depth++;
				} else if (c == '}') {
					depth--;
				}
				pos++;
			}
			if (depth == 0) {
				result.push_back(source.substr(start, pos - start));
			}
		}
		return result;
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

		for (auto& key : {"walk_speed", "run_speed", "eye_height",
		                   "gravity", "jump_velocity",
		                   "damage", "range", "cooldown", "hardness"}) {
			std::string val = extract(key);
			if (!val.empty()) e.fields[key] = val;
		}

		// Living: "playable": True marks a character the user can join as.
		std::string playable = extract("playable");
		if (!playable.empty()) e.fields["playable"] = playable;

		// "collision": {"min": [x,y,z], "max": [x,y,z]}
		// Split into collision_min_[xyz], collision_max_[xyz] for simple consumption.
		auto minVals = extractNestedFloatList(e.source, "collision", "min");
		auto maxVals = extractNestedFloatList(e.source, "collision", "max");
		if (minVals.size() == 3 && maxVals.size() == 3) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%g", minVals[0]); e.fields["collision_min_x"] = buf;
			snprintf(buf, sizeof(buf), "%g", minVals[1]); e.fields["collision_min_y"] = buf;
			snprintf(buf, sizeof(buf), "%g", minVals[2]); e.fields["collision_min_z"] = buf;
			snprintf(buf, sizeof(buf), "%g", maxVals[0]); e.fields["collision_max_x"] = buf;
			snprintf(buf, sizeof(buf), "%g", maxVals[1]); e.fields["collision_max_y"] = buf;
			snprintf(buf, sizeof(buf), "%g", maxVals[2]); e.fields["collision_max_z"] = buf;
		}

		std::string model = extract("model");
		if (!model.empty()) e.fields["model"] = model;

		// Living "stats": {"strength": N, "stamina": N, "agility": N, "intelligence": N}
		// Flatten to stats_strength / stats_stamina / ... so consumers can read scalars.
		{
			auto pos = e.source.find("\"stats\"");
			if (pos != std::string::npos) {
				auto openBrace = e.source.find('{', pos);
				auto closeBrace = std::string::npos;
				if (openBrace != std::string::npos) {
					int depth = 1;
					size_t scan = openBrace + 1;
					while (scan < e.source.size() && depth > 0) {
						if (e.source[scan] == '{') depth++;
						else if (e.source[scan] == '}') { depth--; if (depth == 0) { closeBrace = scan; break; } }
						scan++;
					}
				}
				if (closeBrace != std::string::npos) {
					std::string sub = e.source.substr(openBrace, closeBrace - openBrace + 1);
					for (auto& key : {"strength", "stamina", "agility", "intelligence"}) {
						std::string pat = std::string("\"") + key + "\"";
						auto kp = sub.find(pat);
						if (kp == std::string::npos) continue;
						auto colon = sub.find(':', kp);
						if (colon == std::string::npos) continue;
						size_t vs = colon + 1;
						while (vs < sub.size() && (sub[vs] == ' ' || sub[vs] == '\t')) vs++;
						size_t ve = vs;
						while (ve < sub.size() && sub[ve] != ',' && sub[ve] != '}' &&
						       sub[ve] != '\n' && sub[ve] != ' ') ve++;
						if (ve > vs)
							e.fields[std::string("stats_") + key] = sub.substr(vs, ve - vs);
					}
				}
			}
		}

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

		// Dialog persona — opt-in chat persona for humanoid NPCs. Consumed by
		// the client's DialogPanel; server never sees it (Rule 5).
		std::string dialogSys = extract("dialog_system_prompt");
		if (!dialogSys.empty()) e.fields["dialog_system_prompt"] = dialogSys;
		std::string dialogGreet = extract("dialog_greeting");
		if (!dialogGreet.empty()) e.fields["dialog_greeting"] = dialogGreet;
		std::string dialogTemp = extract("dialog_temperature");
		if (!dialogTemp.empty()) e.fields["dialog_temperature"] = dialogTemp;
		std::string dialogVoice = extract("dialog_voice");
		if (!dialogVoice.empty()) e.fields["dialog_voice"] = dialogVoice;

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

	// "outer": { ..., "inner": [x, y, z], ... } — find the inner list after a
	// nested key. Scoped to the outer key's { ... } block so we don't pick up
	// a same-named list from a sibling section.
	static std::vector<float> extractNestedFloatList(const std::string& source,
	                                                 const std::string& outerKey,
	                                                 const std::string& innerKey) {
		std::string outerPat = "\"" + outerKey + "\"";
		auto pos = source.find(outerPat);
		if (pos == std::string::npos) return {};
		auto openBrace = source.find('{', pos + outerPat.size());
		if (openBrace == std::string::npos) return {};
		// Match the closing brace (flat — no deeper nesting expected here).
		int depth = 1;
		size_t scan = openBrace + 1;
		while (scan < source.size() && depth > 0) {
			if (source[scan] == '{') depth++;
			else if (source[scan] == '}') depth--;
			if (depth == 0) break;
			scan++;
		}
		if (scan >= source.size()) return {};
		std::string sub = source.substr(openBrace, scan - openBrace);
		return extractFloatList(sub, innerKey);
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
	std::vector<ArtifactEntry> m_entries;
};

} // namespace civcraft

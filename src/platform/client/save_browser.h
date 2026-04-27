#pragma once

// SaveBrowser — client-side scan of saves/<dir>/world.json entries.
//
// Server-side WorldManager (server/world_manager.h) writes world.json into
// each save directory; the client doesn't share that code (server is a
// separate process), so we read the JSON directly. Hand-rolled minimal
// parser — these files are 8 fields, all top-level scalars, so a 30-line
// reader avoids pulling in nlohmann/json (same call we made for Settings).
//
// Used by the Singleplayer flow: list existing saves on the slot picker,
// or fall through to the World picker for "New World".

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace solarium::vk {

struct SaveEntry {
	std::string name;
	std::string path;          // absolute or build-rel; passed verbatim to --world
	std::string templateName;  // pretty name from world.json
	int         templateIndex = 0;
	int         seed = 42;
	std::string lastPlayed;    // ISO timestamp from world.json
};

// Create a fresh save dir + world.json with the given metadata. Returns
// the resolved path on success ("saves/<sanitised_name>"), or empty on
// failure. The directory is empty otherwise (no chunks yet) — the server
// runs procgen from the seed/template the first time and persists the
// modified chunks back on shutdown via saveWorldIfNeeded.
inline std::string createSave(const std::string& savesDir,
                              const std::string& name,
                              int seed, int templateIndex,
                              const std::string& templateName) {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::create_directories(savesDir, ec);

	// Sanitise: alnum + underscore + dash only. Avoids slash injection
	// and weird filesystem chars across platforms.
	std::string slug;
	for (char c : name) {
		if (std::isalnum((unsigned char)c)) slug += c;
		else if (c == ' ' || c == '_' || c == '-') slug += '_';
	}
	if (slug.empty()) slug = "world";

	// Disambiguate: if the dir already exists, append _2, _3, ...
	std::string base = savesDir + "/" + slug;
	std::string path = base;
	int n = 2;
	while (fs::exists(path, ec)) {
		path = base + "_" + std::to_string(n++);
		if (n > 9999) return "";  // pathological
	}

	if (!fs::create_directories(path, ec)) return "";

	std::ofstream f(path + "/world.json");
	if (!f.is_open()) return "";

	std::time_t now = std::time(nullptr);
	char timeBuf[64];
	std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S",
	              std::localtime(&now));

	f << "{\n";
	f << "  \"name\": \""          << name << "\",\n";
	f << "  \"seed\": "            << seed << ",\n";
	f << "  \"templateIndex\": "   << templateIndex << ",\n";
	f << "  \"templateName\": \""  << templateName << "\",\n";
	f << "  \"gameMode\": \"survival\",\n";
	f << "  \"worldTime\": 0.25,\n";
	f << "  \"spawnPos\": [30, 5, 30],\n";
	f << "  \"lastPlayed\": \""    << timeBuf << "\",\n";
	f << "  \"version\": 1\n";
	f << "}\n";
	return path;
}

// Remove a save directory recursively. Returns the number of entries
// removed (0 = path didn't exist).
inline std::uintmax_t deleteSave(const std::string& path) {
	std::error_code ec;
	return std::filesystem::remove_all(path, ec);
}

inline std::vector<SaveEntry> scanSaves(const std::string& savesDir) {
	namespace fs = std::filesystem;
	std::vector<SaveEntry> out;
	std::error_code ec;
	if (!fs::exists(savesDir, ec)) return out;

	for (auto& dir : fs::directory_iterator(savesDir, ec)) {
		if (!dir.is_directory()) continue;
		std::string p = dir.path().string();
		std::string meta = p + "/world.json";
		std::ifstream f(meta);
		if (!f.is_open()) continue;
		std::stringstream ss; ss << f.rdbuf();
		std::string text = ss.str();

		// Minimal parser: locate `"key":<value>` pairs. Tolerant of
		// trailing whitespace, surrounding quotes for strings.
		auto extract = [&](const std::string& key) -> std::string {
			std::string needle = "\"" + key + "\"";
			auto pos = text.find(needle);
			if (pos == std::string::npos) return "";
			pos = text.find(':', pos + needle.size());
			if (pos == std::string::npos) return "";
			++pos;
			while (pos < text.size() && (text[pos]==' '||text[pos]=='\t')) ++pos;
			if (pos >= text.size()) return "";
			if (text[pos] == '"') {
				++pos;
				auto end = text.find('"', pos);
				return text.substr(pos, end == std::string::npos ? 0 : end - pos);
			}
			auto end = pos;
			while (end < text.size() && text[end] != ',' && text[end] != '\n'
			       && text[end] != '}') ++end;
			std::string s = text.substr(pos, end - pos);
			while (!s.empty() && (s.back()==' '||s.back()=='\t')) s.pop_back();
			return s;
		};

		SaveEntry e;
		e.path = p;
		e.name = extract("name");
		if (e.name.empty()) e.name = dir.path().filename().string();
		e.templateName = extract("templateName");
		e.lastPlayed   = extract("lastPlayed");
		try { e.templateIndex = std::stoi(extract("templateIndex")); } catch (...) {}
		try { e.seed          = std::stoi(extract("seed")); } catch (...) {}
		out.push_back(std::move(e));
	}
	// Newest first.
	std::sort(out.begin(), out.end(),
		[](const SaveEntry& a, const SaveEntry& b) {
			return a.lastPlayed > b.lastPlayed;
		});
	return out;
}

} // namespace solarium::vk

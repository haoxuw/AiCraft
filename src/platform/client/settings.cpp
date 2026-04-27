#include "client/settings.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace solarium {

namespace {

// Resolve once. Stable for the process lifetime — even if the user changes
// $HOME mid-session, we keep the original.
const std::string& computePath() {
	static std::string cached = []{
		std::string base;
		if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
			base = std::string(xdg) + "/solarium";
		} else if (const char* home = std::getenv("HOME"); home && *home) {
			base = std::string(home) + "/.solarium";
		} else {
			base = "/tmp/solarium";  // last-resort fallback
		}
		return base + "/settings.json";
	}();
	return cached;
}

// ── Tiny flat-JSON parser ─────────────────────────────────────────────────
// Accepts: { "k1": v1, "k2": v2 } where v ∈ {number, true, false, "string"}.
// Whitespace and trailing commas tolerated. Unknown keys are silently
// dropped — keeps forward compat when older builds read newer files.

struct Lexer {
	const char* p;
	const char* end;
	void skipWs() {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
	}
	bool match(char c) { skipWs(); if (p < end && *p == c) { ++p; return true; } return false; }
	bool atEnd() const { return p >= end; }
};

bool parseString(Lexer& L, std::string& out) {
	L.skipWs();
	if (L.p >= L.end || *L.p != '"') return false;
	++L.p;
	out.clear();
	while (L.p < L.end && *L.p != '"') {
		if (*L.p == '\\' && L.p + 1 < L.end) {
			char c = L.p[1];
			if      (c == 'n') out += '\n';
			else if (c == 't') out += '\t';
			else if (c == 'r') out += '\r';
			else               out += c;     // literal: \", \\, \/
			L.p += 2;
		} else {
			out += *L.p++;
		}
	}
	if (L.p < L.end && *L.p == '"') { ++L.p; return true; }
	return false;
}

// Parses one of: 1.5, -3, true, false, "hello". Stores into the matching
// out-param. Returns the type tag. Number parsing uses strtod for forgiving
// behavior on integers / scientific notation.
enum class ValType { None, Number, Bool, String };
ValType parseValue(Lexer& L, double& outNum, bool& outBool, std::string& outStr) {
	L.skipWs();
	if (L.atEnd()) return ValType::None;
	if (*L.p == '"') return parseString(L, outStr) ? ValType::String : ValType::None;
	if (L.end - L.p >= 4 && std::strncmp(L.p, "true", 4) == 0) {
		L.p += 4; outBool = true; return ValType::Bool;
	}
	if (L.end - L.p >= 5 && std::strncmp(L.p, "false", 5) == 0) {
		L.p += 5; outBool = false; return ValType::Bool;
	}
	// Number: scan to comma / brace / whitespace, hand to strtod.
	const char* s = L.p;
	while (L.p < L.end && *L.p != ',' && *L.p != '}' &&
	       *L.p != ' ' && *L.p != '\n' && *L.p != '\r' && *L.p != '\t') ++L.p;
	std::string token(s, L.p - s);
	char* endPtr = nullptr;
	outNum = std::strtod(token.c_str(), &endPtr);
	if (endPtr == token.c_str()) return ValType::None;
	return ValType::Number;
}

} // namespace

const std::string& Settings::path() { return computePath(); }

Settings Settings::load() {
	Settings s;  // defaults
	std::ifstream f(computePath());
	if (!f.is_open()) return s;  // first run

	std::stringstream ss; ss << f.rdbuf();
	std::string text = ss.str();
	Lexer L{text.data(), text.data() + text.size()};

	if (!L.match('{')) return s;
	while (true) {
		L.skipWs();
		if (L.match('}')) break;
		std::string key;
		if (!parseString(L, key)) {
			std::fprintf(stderr, "[Settings] parse error in %s: expected key\n",
			             computePath().c_str());
			return s;
		}
		if (!L.match(':')) {
			std::fprintf(stderr, "[Settings] parse error: expected ':' after %s\n",
			             key.c_str());
			return s;
		}
		double n = 0; bool b = false; std::string str;
		ValType t = parseValue(L, n, b, str);
		if (t == ValType::None) return s;

		// Field dispatch — order doesn't matter, missing fields keep defaults,
		// unknown keys are dropped (forward compat).
		if      (key == "master_volume"   && t == ValType::Number) s.master_volume   = (float)n;
		else if (key == "music_volume"    && t == ValType::Number) s.music_volume    = (float)n;
		else if (key == "sim_speed_cap"   && t == ValType::Number) s.sim_speed_cap   = (float)n;
		else if (key == "music_enabled"   && t == ValType::Bool)   s.music_enabled   = b;
		else if (key == "footsteps_muted" && t == ValType::Bool)   s.footsteps_muted = b;
		else if (key == "effects_muted"   && t == ValType::Bool)   s.effects_muted   = b;
		else if (key == "lan_visible"     && t == ValType::Bool)   s.lan_visible     = b;

		L.skipWs();
		if (L.match(',')) continue;
		if (L.match('}')) break;
	}
	return s;
}

bool Settings::save() const {
	const std::string& p = computePath();
	std::error_code ec;
	std::filesystem::create_directories(
		std::filesystem::path(p).parent_path(), ec);

	// Atomic write: temp + rename. If the user-edits-mid-write, they
	// either get the old or the new file, never a torn one.
	std::string tmp = p + ".tmp";
	std::ofstream f(tmp);
	if (!f.is_open()) {
		std::fprintf(stderr, "[Settings] cannot open %s for write\n", tmp.c_str());
		return false;
	}
	f << "{\n";
	f << "  \"master_volume\": "   << master_volume   << ",\n";
	f << "  \"music_volume\": "    << music_volume    << ",\n";
	f << "  \"music_enabled\": "   << (music_enabled   ? "true" : "false") << ",\n";
	f << "  \"footsteps_muted\": " << (footsteps_muted ? "true" : "false") << ",\n";
	f << "  \"effects_muted\": "   << (effects_muted   ? "true" : "false") << ",\n";
	f << "  \"lan_visible\": "     << (lan_visible     ? "true" : "false") << ",\n";
	f << "  \"sim_speed_cap\": "   << sim_speed_cap << "\n";
	f << "}\n";
	f.close();
	if (!f) {
		std::fprintf(stderr, "[Settings] write failed for %s\n", tmp.c_str());
		std::filesystem::remove(tmp, ec);
		return false;
	}
	std::filesystem::rename(tmp, p, ec);
	if (ec) {
		std::fprintf(stderr, "[Settings] rename %s -> %s failed: %s\n",
		             tmp.c_str(), p.c_str(), ec.message().c_str());
		return false;
	}
	return true;
}

} // namespace solarium

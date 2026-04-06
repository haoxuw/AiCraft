#pragma once

/**
 * Load BoxModel definitions from Python model files (artifacts/models/).
 *
 * Parses the subset of Python used in model dicts: numbers, lists, dicts,
 * strings, math.pi, and comments.  No Python interpreter required.
 *
 * Lookup order: player/ (user overrides) → base/ (built-in) → C++ fallback.
 */

#include "client/box_model.h"
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <vector>
#include <filesystem>

namespace modcraft {

namespace model_loader {

// ---- Minimal tokenizer for Python model dicts ----

enum class Tok { Number, String, LBrace, RBrace, LBracket, RBracket,
                 Comma, Colon, Ident, End };

struct Token {
	Tok type;
	double numVal = 0;
	std::string strVal;
};

struct Lexer {
	std::string src;
	size_t pos = 0;

	char peek() const { return pos < src.size() ? src[pos] : '\0'; }
	char advance() { return pos < src.size() ? src[pos++] : '\0'; }

	void skipWS() {
		while (pos < src.size()) {
			char c = src[pos];
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { pos++; continue; }
			if (c == '#') { while (pos < src.size() && src[pos] != '\n') pos++; continue; }
			break;
		}
	}

	Token next() {
		skipWS();
		if (pos >= src.size()) return {Tok::End};
		char c = peek();
		if (c == '{') { advance(); return {Tok::LBrace}; }
		if (c == '}') { advance(); return {Tok::RBrace}; }
		if (c == '[') { advance(); return {Tok::LBracket}; }
		if (c == ']') { advance(); return {Tok::RBracket}; }
		if (c == ',') { advance(); return {Tok::Comma}; }
		if (c == ':') { advance(); return {Tok::Colon}; }
		if (c == '"' || c == '\'') {
			char q = advance();
			std::string s;
			while (pos < src.size() && src[pos] != q) s += advance();
			if (pos < src.size()) advance(); // closing quote
			return {Tok::String, 0, s};
		}
		if (c == '-' || c == '+' || (c >= '0' && c <= '9') || c == '.') {
			std::string num;
			if (c == '-' || c == '+') num += advance();
			while (pos < src.size() && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.'))
				num += advance();
			if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
				num += advance();
				if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) num += advance();
				while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') num += advance();
			}
			return {Tok::Number, std::stod(num)};
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
			std::string id;
			while (pos < src.size() && ((src[pos] >= 'a' && src[pos] <= 'z') ||
			       (src[pos] >= 'A' && src[pos] <= 'Z') || (src[pos] >= '0' && src[pos] <= '9') ||
			       src[pos] == '_' || src[pos] == '.'))
				id += advance();
			// Resolve known constants
			if (id == "math.pi") return {Tok::Number, M_PI};
			if (id == "True") return {Tok::Number, 1.0};
			if (id == "False") return {Tok::Number, 0.0};
			return {Tok::Ident, 0, id};
		}
		advance(); // skip unknown
		return next();
	}
};

// ---- Parsed value types ----

struct Value;
using Dict = std::vector<std::pair<std::string, Value>>;
using List = std::vector<Value>;

struct Value {
	enum Type { NONE, NUM, STR, LIST, DICT } type = NONE;
	double num = 0;
	std::string str;
	List list;
	Dict dict;

	double getNum(double def = 0) const { return type == NUM ? num : def; }
	const std::string& getStr(const std::string& def = {}) const {
		static const std::string empty;
		return type == STR ? str : (def.empty() ? empty : def);
	}
};

// ---- Recursive descent parser ----

struct Parser {
	Lexer lex;
	Token cur;

	void advance() { cur = lex.next(); }

	Value parseValue() {
		if (cur.type == Tok::Number) {
			Value v; v.type = Value::NUM; v.num = cur.numVal;
			advance();
			return v;
		}
		if (cur.type == Tok::String) {
			Value v; v.type = Value::STR; v.str = cur.strVal;
			advance();
			return v;
		}
		if (cur.type == Tok::LBracket) {
			advance(); // skip [
			Value v; v.type = Value::LIST;
			while (cur.type != Tok::RBracket && cur.type != Tok::End) {
				v.list.push_back(parseValue());
				if (cur.type == Tok::Comma) advance();
			}
			if (cur.type == Tok::RBracket) advance();
			return v;
		}
		if (cur.type == Tok::LBrace) {
			advance(); // skip {
			Value v; v.type = Value::DICT;
			while (cur.type != Tok::RBrace && cur.type != Tok::End) {
				std::string key;
				if (cur.type == Tok::String) { key = cur.strVal; advance(); }
				else if (cur.type == Tok::Ident) { key = cur.strVal; advance(); }
				else { advance(); continue; }
				if (cur.type == Tok::Colon) advance();
				v.dict.push_back({key, parseValue()});
				if (cur.type == Tok::Comma) advance();
			}
			if (cur.type == Tok::RBrace) advance();
			return v;
		}
		// Unary minus applied to identifier (e.g., -math.pi)
		if (cur.type == Tok::Ident) {
			Value v; v.type = Value::STR; v.str = cur.strVal;
			advance();
			return v;
		}
		advance();
		return {};
	}

	Value parse() {
		advance();
		// Skip to 'model = {' or just find the first dict
		while (cur.type != Tok::End) {
			if (cur.type == Tok::Ident && cur.strVal == "model") {
				advance(); // skip 'model'
				// skip '='
				if (cur.type == Tok::Ident || cur.type == Tok::Number) advance();
				// Check if we're looking at something that's not the dict
				// The '=' sign is parsed as unknown and skipped
				break;
			}
			if (cur.type == Tok::LBrace) break;
			advance();
		}
		return parseValue();
	}
};

// ---- Convert parsed dict to BoxModel ----

inline glm::vec3 toVec3(const List& l, glm::vec3 def = {0,0,0}) {
	if (l.size() >= 3)
		return {(float)l[0].getNum(), (float)l[1].getNum(), (float)l[2].getNum()};
	return def;
}

inline glm::vec4 toVec4(const List& l, glm::vec4 def = {1,1,1,1}) {
	if (l.size() >= 4)
		return {(float)l[0].getNum(), (float)l[1].getNum(),
		        (float)l[2].getNum(), (float)l[3].getNum()};
	if (l.size() == 3)
		return {(float)l[0].getNum(), (float)l[1].getNum(), (float)l[2].getNum(), 1.0f};
	return def;
}

inline const Value* dictGet(const Dict& d, const std::string& key) {
	for (auto& [k, v] : d) if (k == key) return &v;
	return nullptr;
}

inline BoxModel dictToBoxModel(const Dict& d) {
	BoxModel m;

	if (auto* v = dictGet(d, "height"))    m.totalHeight = (float)v->getNum(1.0);
	if (auto* v = dictGet(d, "scale"))     m.modelScale = (float)v->getNum(1.0);
	if (auto* v = dictGet(d, "walk_speed")) m.walkCycleSpeed = (float)v->getNum(8.0);
	if (auto* v = dictGet(d, "idle_bob"))  m.idleBobAmount = (float)v->getNum(0.01);
	if (auto* v = dictGet(d, "walk_bob"))  m.walkBobAmount = (float)v->getNum(0.03);
	if (auto* v = dictGet(d, "idle_bob_speed")) m.idleBobSpeed = (float)v->getNum(1.5);

	auto* parts = dictGet(d, "parts");
	if (!parts || parts->type != Value::LIST) return m;

	for (auto& pv : parts->list) {
		if (pv.type != Value::DICT) continue;
		auto& pd = pv.dict;

		BodyPart part;

		// offset (center) and size (full size → halfSize)
		if (auto* v = dictGet(pd, "offset")) part.offset = toVec3(v->list);
		if (auto* v = dictGet(pd, "size")) {
			glm::vec3 full = toVec3(v->list);
			part.halfSize = full * 0.5f;
		}
		if (auto* v = dictGet(pd, "color")) part.color = toVec4(v->list);

		// Animation
		if (auto* v = dictGet(pd, "pivot"))      part.pivot = toVec3(v->list);
		if (auto* v = dictGet(pd, "swing_axis"))  part.swingAxis = toVec3(v->list, {1,0,0});
		if (auto* v = dictGet(pd, "amplitude"))   part.swingAmplitude = (float)v->getNum();
		if (auto* v = dictGet(pd, "phase"))       part.swingPhase = (float)v->getNum();
		if (auto* v = dictGet(pd, "speed"))       part.swingSpeed = (float)v->getNum(1.0);

		m.parts.push_back(part);
	}

	// Parse equip transform (how item is held in hand)
	auto* equipDict = dictGet(d, "equip");
	if (equipDict && equipDict->type == Value::DICT) {
		auto& ed = equipDict->dict;
		if (auto* v = dictGet(ed, "offset"))   m.equip.offset = toVec3(v->list);
		if (auto* v = dictGet(ed, "rotation")) m.equip.rotation = toVec3(v->list);
		if (auto* v = dictGet(ed, "scale"))    m.equip.scale = (float)v->getNum(1.0);
	}

	// Parse hand attachment points (character models define where their hands are)
	if (auto* v = dictGet(d, "hand_r"))  m.handR  = toVec3(v->list);
	if (auto* v = dictGet(d, "hand_l"))  m.handL  = toVec3(v->list);
	if (auto* v = dictGet(d, "pivot_r")) m.pivotR = toVec3(v->list);
	if (auto* v = dictGet(d, "pivot_l")) m.pivotL = toVec3(v->list);

	return m;
}

// ---- File loading ----

inline bool loadModelFile(const std::string& path, BoxModel& out) {
	std::ifstream f(path);
	if (!f.is_open()) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	std::string src = ss.str();
	if (src.empty()) return false;

	Parser parser;
	parser.lex.src = std::move(src);
	Value root = parser.parse();
	if (root.type != Value::DICT || root.dict.empty()) return false;

	out = dictToBoxModel(root.dict);
	return !out.parts.empty();
}

/**
 * Load a model by name, checking player/ override first, then base/.
 * Returns true if a Python model was found and loaded.
 */
inline bool loadModel(const std::string& artifactsDir, const std::string& name, BoxModel& out) {
	// Player override first
	std::string playerPath = artifactsDir + "/models/player/" + name + ".py";
	if (loadModelFile(playerPath, out)) return true;
	// Built-in
	std::string basePath = artifactsDir + "/models/base/" + name + ".py";
	if (loadModelFile(basePath, out)) return true;
	return false;
}

/**
 * Load all models, using Python files where available, C++ builtins as fallback.
 * Returns a map of name → BoxModel.
 */
inline std::unordered_map<std::string, BoxModel> loadAllModels(const std::string& artifactsDir) {
	std::unordered_map<std::string, BoxModel> models;

	// Scan artifacts/models/base/ and artifacts/models/player/ for all .py files
	for (auto* sub : {"base", "player"}) {
		std::string dir = artifactsDir + "/models/" + sub;
		if (!std::filesystem::exists(dir)) continue;
		for (auto& entry : std::filesystem::directory_iterator(dir)) {
			if (entry.path().extension() != ".py") continue;
			std::string name = entry.path().stem().string();
			BoxModel m;
			if (loadModel(artifactsDir, name, m))
				models[name] = std::move(m);
		}
	}

	return models;
}

} // namespace model_loader
} // namespace modcraft

#pragma once

// Parses the Python subset used in model dicts: numbers, lists, dicts,
// strings, math.pi, comments. Lookup: base/ → C++ fallback.

#include "client/box_model.h"
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <vector>
#include <filesystem>

namespace civcraft {

namespace model_loader {

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
			if (pos < src.size()) advance();
			return {Tok::String, 0, s};
		}
		if (c == '-' || c == '+' || (c >= '0' && c <= '9') || c == '.') {
			std::string num;
			if (c == '-' || c == '+') num += advance();
			while (pos < src.size() && ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.'))
				num += advance();
			// Bare `-` before ident, or `...` in docstring — not a number.
			bool hasDigit = false;
			for (char nc : num) if (nc >= '0' && nc <= '9') { hasDigit = true; break; }
			if (!hasDigit) return next();
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
			if (id == "math.pi") return {Tok::Number, M_PI};
			if (id == "True") return {Tok::Number, 1.0};
			if (id == "False") return {Tok::Number, 0.0};
			return {Tok::Ident, 0, id};
		}
		advance();
		return next();
	}
};

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
			advance();
			Value v; v.type = Value::LIST;
			while (cur.type != Tok::RBracket && cur.type != Tok::End) {
				v.list.push_back(parseValue());
				if (cur.type == Tok::Comma) advance();
			}
			if (cur.type == Tok::RBracket) advance();
			return v;
		}
		if (cur.type == Tok::LBrace) {
			advance();
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
		// Skip to 'model = {' or the first dict.
		while (cur.type != Tok::End) {
			if (cur.type == Tok::Ident && cur.strVal == "model") {
				advance();
				if (cur.type == Tok::Ident || cur.type == Tok::Number) advance();
				break;
			}
			if (cur.type == Tok::LBrace) break;
			advance();
		}
		return parseValue();
	}
};

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

		if (auto* v = dictGet(pd, "name")) part.name = v->getStr();
		if (auto* v = dictGet(pd, "role")) part.role = v->getStr();

		// size = full size → halfSize
		if (auto* v = dictGet(pd, "offset")) part.offset = toVec3(v->list);
		if (auto* v = dictGet(pd, "size")) {
			glm::vec3 full = toVec3(v->list);
			part.halfSize = full * 0.5f;
		}
		if (auto* v = dictGet(pd, "color")) part.color = toVec4(v->list);

		if (auto* v = dictGet(pd, "pivot"))      part.pivot = toVec3(v->list);
		if (auto* v = dictGet(pd, "swing_axis"))  part.swingAxis = toVec3(v->list, {1,0,0});
		if (auto* v = dictGet(pd, "amplitude"))   part.swingAmplitude = (float)v->getNum();
		if (auto* v = dictGet(pd, "phase"))       part.swingPhase = (float)v->getNum();
		if (auto* v = dictGet(pd, "speed"))       part.swingSpeed = (float)v->getNum(1.0);

		if (auto* v = dictGet(pd, "head")) part.isHead = (v->getNum(0.0) != 0.0);

		m.parts.push_back(part);
	}

	if (auto* v = dictGet(d, "head_pivot")) m.headPivot = toVec3(v->list, m.headPivot);

	// clips = { "mine": { "right_arm": {"amp":80, "bias":-30, ...}, ... }, ... }
	auto* clipsDict = dictGet(d, "clips");
	if (clipsDict && clipsDict->type == Value::DICT) {
		for (auto& [clipName, clipVal] : clipsDict->dict) {
			if (clipVal.type != Value::DICT) continue;
			AnimClip clip;
			for (auto& [partName, overrideVal] : clipVal.dict) {
				if (overrideVal.type != Value::DICT) continue;
				auto& od = overrideVal.dict;
				ClipOverride ov;
				if (auto* v = dictGet(od, "axis"))  ov.axis = toVec3(v->list, {1, 0, 0});
				if (auto* v = dictGet(od, "amp"))   ov.amplitude = (float)v->getNum();
				if (auto* v = dictGet(od, "amplitude")) ov.amplitude = (float)v->getNum();
				if (auto* v = dictGet(od, "phase")) ov.phase = (float)v->getNum();
				if (auto* v = dictGet(od, "bias"))  ov.bias = (float)v->getNum();
				if (auto* v = dictGet(od, "speed")) ov.speed = (float)v->getNum(1.0);
				clip.overrides[partName] = ov;
			}
			m.clips[clipName] = std::move(clip);
		}
	}

	auto* equipDict = dictGet(d, "equip");
	if (equipDict && equipDict->type == Value::DICT) {
		auto& ed = equipDict->dict;
		if (auto* v = dictGet(ed, "offset"))   m.equip.offset = toVec3(v->list);
		if (auto* v = dictGet(ed, "rotation")) m.equip.rotation = toVec3(v->list);
		if (auto* v = dictGet(ed, "scale"))    m.equip.scale = (float)v->getNum(1.0);
	}

	// Fold equip.scale into modelScale so the model has one authoritative
	// size that applies in every render context (hand, ground, inventory).
	// equip.offset/rotation stay equip-only — they position/orient the item
	// against the wielder's hand and don't make sense for ground/inventory.
	m.modelScale *= m.equip.scale;
	m.equip.scale = 1.0f;

	if (auto* v = dictGet(d, "hand_r"))  m.handR  = toVec3(v->list);
	if (auto* v = dictGet(d, "hand_l"))  m.handL  = toVec3(v->list);
	if (auto* v = dictGet(d, "pivot_r")) m.pivotR = toVec3(v->list);
	if (auto* v = dictGet(d, "pivot_l")) m.pivotL = toVec3(v->list);

	return m;
}

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

inline bool loadModel(const std::string& artifactsDir, const std::string& name, BoxModel& out) {
	std::string basePath = artifactsDir + "/models/base/" + name + ".py";
	return loadModelFile(basePath, out);
}

// Clone base, apply color overrides by role, drop parts in hide list.
// Variant dict: { "fur": [rgba], "stripe": [rgba], "hide": ["stripe"] }
inline BoxModel bakeVariant(const BoxModel& base, const Dict& variant) {
	BoxModel out = base;

	std::vector<std::string> hide;
	if (auto* v = dictGet(variant, "hide"); v && v->type == Value::LIST) {
		for (auto& e : v->list)
			if (e.type == Value::STR) hide.push_back(e.str);
	}
	auto isHidden = [&](const std::string& role) {
		if (role.empty()) return false;
		for (auto& h : hide) if (h == role) return true;
		return false;
	};

	std::unordered_map<std::string, glm::vec4> roleColor;
	for (auto& [k, v] : variant) {
		if (k == "hide") continue;
		if (v.type == Value::LIST) roleColor[k] = toVec4(v.list);
	}

	std::vector<BodyPart> kept;
	kept.reserve(out.parts.size());
	for (auto& p : out.parts) {
		if (isHidden(p.role)) continue;
		auto it = roleColor.find(p.role);
		if (it != roleColor.end()) p.color = it->second;
		kept.push_back(p);
	}
	out.parts = std::move(kept);
	return out;
}

// Also extracts top-level `variants = [...]` into outVariants for caller to bake.
inline bool loadModelFileWithVariants(const std::string& path, BoxModel& out,
                                      std::vector<Dict>& outVariants) {
	std::ifstream f(path);
	if (!f.is_open()) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	std::string src = ss.str();
	if (src.empty()) return false;

	Parser parser;
	parser.lex.src = std::move(src);
	Value root;
	try {
		root = parser.parse();
	} catch (const std::exception& e) {
		fprintf(stderr, "[model_loader] parse failed for %s: %s\n", path.c_str(), e.what());
		return false;
	}
	if (root.type != Value::DICT || root.dict.empty()) return false;

	out = dictToBoxModel(root.dict);

	while (parser.cur.type != Tok::End) {
		if (parser.cur.type == Tok::Ident && parser.cur.strVal == "variants") {
			parser.advance();
			Value v = parser.parseValue();
			if (v.type == Value::LIST) {
				for (auto& ve : v.list)
					if (ve.type == Value::DICT) outVariants.push_back(ve.dict);
			}
			break;
		}
		parser.advance();
	}

	return !out.parts.empty();
}

// Variants emit `name#0`, `name#1`, ...; `name` resolves to variant 0.
inline std::unordered_map<std::string, BoxModel> loadAllModels(const std::string& artifactsDir) {
	std::unordered_map<std::string, BoxModel> models;

	std::string dir = artifactsDir + "/models/base";
	if (!std::filesystem::exists(dir)) return models;

	for (auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() != ".py") continue;
		std::string name = entry.path().stem().string();

		BoxModel m;
		std::vector<Dict> variants;
		if (!loadModelFileWithVariants(entry.path().string(), m, variants)) continue;

		if (variants.empty()) {
			models[name] = std::move(m);
		} else {
			for (size_t i = 0; i < variants.size(); ++i) {
				BoxModel baked = bakeVariant(m, variants[i]);
				models[name + "#" + std::to_string(i)] = baked;
			}
			// Default lookup (`name`) → variant 0
			models[name] = models[name + "#0"];
		}
	}

	return models;
}

// Returns 0 if only the plain `name` key exists (no variants).
inline int countVariants(const std::unordered_map<std::string, BoxModel>& models,
                         const std::string& name) {
	int n = 0;
	while (models.count(name + "#" + std::to_string(n))) ++n;
	return n;
}

} // namespace model_loader
} // namespace civcraft

#pragma once

// Loads a rig-template artifact (`src/artifacts/rigs/<ns>/<id>.py`) into a
// `Rig` struct. Reuses the model_loader.h tokenizer-only Python parser so
// the same constraints apply: literals only, no variable refs, math.pi OK,
// True/False normalised to 1.0/0.0.
//
// File format (matches docs/MODEL_PIPELINE.md §"Rig template"):
//
//   template = {
//       "id": "humanoid",
//       "bones": [
//           {"name": "root",        "parent": None,    "default_pos": [0, 0, 0]},
//           {"name": "torso",       "parent": "root",  "default_pos": [0, 1.0, 0]},
//           …
//       ],
//       "clips": {
//           "walk": {
//               "duration": 1.0,
//               "channels": [
//                   {"bone": "l_leg_upper", "axis": [1, 0, 0],
//                    "keys": [{"t": 0.0, "deg": 30}, {"t": 0.5, "deg": -30}, {"t": 1.0, "deg": 30}]},
//                   …
//               ],
//           },
//           …
//       },
//   }
//
// `parent: None` is parsed as the empty string (Tok::Ident "None" yields a
// string-typed Value). Missing fields fall back to defaults from rig.h.

#include "client/model_loader.h"
#include "client/rig.h"
#include <fstream>
#include <sstream>

namespace solarium {

namespace rig_loader {

inline Rig dictToRig(const model_loader::Dict& d) {
	using namespace model_loader;
	Rig r;

	if (auto* v = dictGet(d, "id")) r.id = v->getStr();

	if (auto* bonesV = dictGet(d, "bones"); bonesV && bonesV->type == Value::LIST) {
		r.bones.reserve(bonesV->list.size());
		for (auto& bv : bonesV->list) {
			if (bv.type != Value::DICT) continue;
			Bone b;
			if (auto* v = dictGet(bv.dict, "name"))   b.name   = v->getStr();
			if (auto* v = dictGet(bv.dict, "parent")) {
				// "None" arrives as a string-typed Value via the Ident path.
				const std::string& s = v->getStr();
				b.parent = (s == "None") ? "" : s;
			}
			if (auto* v = dictGet(bv.dict, "default_pos")) b.defaultPos = toVec3(v->list);
			r.bones.push_back(std::move(b));
		}
	}

	if (auto* clipsV = dictGet(d, "clips"); clipsV && clipsV->type == Value::DICT) {
		for (auto& [clipName, clipVal] : clipsV->dict) {
			if (clipVal.type != Value::DICT) continue;
			KeyframeClip clip;
			if (auto* v = dictGet(clipVal.dict, "duration"))
				clip.duration = (float)v->getNum(1.0);

			auto* chsV = dictGet(clipVal.dict, "channels");
			if (!chsV || chsV->type != Value::LIST) {
				r.clips[clipName] = std::move(clip);
				continue;
			}
			clip.channels.reserve(chsV->list.size());
			for (auto& chv : chsV->list) {
				if (chv.type != Value::DICT) continue;
				KeyframeChannel ch;
				if (auto* v = dictGet(chv.dict, "bone")) ch.boneName = v->getStr();
				if (auto* v = dictGet(chv.dict, "axis")) ch.axis = toVec3(v->list, {1, 0, 0});
				if (auto* keysV = dictGet(chv.dict, "keys");
				    keysV && keysV->type == Value::LIST) {
					ch.keys.reserve(keysV->list.size());
					for (auto& kv : keysV->list) {
						if (kv.type != Value::DICT) continue;
						KeyframeRot k;
						if (auto* v = dictGet(kv.dict, "t"))   k.t   = (float)v->getNum();
						if (auto* v = dictGet(kv.dict, "deg")) k.deg = (float)v->getNum();
						ch.keys.push_back(k);
					}
				}
				clip.channels.push_back(std::move(ch));
			}
			r.clips[clipName] = std::move(clip);
		}
	}

	return r;
}

inline bool loadRigFile(const std::string& path, Rig& out) {
	std::ifstream f(path);
	if (!f.is_open()) return false;
	std::stringstream ss;
	ss << f.rdbuf();

	model_loader::Parser p;
	p.lex.src = ss.str();

	// Parser::parse() looks for `model = {...}`; for rigs we look for
	// `template = {...}`. Mirror parse() but with the template keyword.
	p.advance();
	while (p.cur.type != model_loader::Tok::End) {
		if (p.cur.type == model_loader::Tok::Ident && p.cur.strVal == "template") {
			p.advance();
			if (p.cur.type == model_loader::Tok::Ident
			    || p.cur.type == model_loader::Tok::Number) {
				p.advance();   // consume `=`-as-ident in lex sense
			}
			break;
		}
		if (p.cur.type == model_loader::Tok::LBrace) break;
		p.advance();
	}

	model_loader::Value root = p.parseValue();
	if (root.type != model_loader::Value::DICT) return false;

	out = dictToRig(root.dict);
	return true;
}

} // namespace rig_loader

} // namespace solarium

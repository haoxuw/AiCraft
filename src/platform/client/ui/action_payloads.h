#pragma once

// Typed parsers for the multi-field action payloads exchanged between
// CEF page builders (which concat colon-separated strings in JS) and
// ActionRouter handlers (which used to splice them inline with
// body.find(':') arithmetic at every call site).
//
// One canonical parser per action means schema drift between the JS
// builder and the C++ consumer surfaces as a parse failure (returns
// nullopt) instead of a silent off-by-one substring.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace solarium::ui {

// Generic helper: split body on ':' into at most maxParts pieces.
// maxParts = -1 means split exhaustively. The last piece keeps any
// trailing colons (so a base64 blob with '/' / '+' / '=' survives).
inline std::vector<std::string> splitColon(const std::string& body, int maxParts = -1) {
	std::vector<std::string> out;
	size_t p = 0;
	while (p <= body.size()) {
		if (maxParts > 0 && (int)out.size() == maxParts - 1) {
			out.push_back(body.substr(p));
			break;
		}
		auto q = body.find(':', p);
		out.push_back(body.substr(p,
			q == std::string::npos ? std::string::npos : q - p));
		if (q == std::string::npos) break;
		p = q + 1;
	}
	return out;
}

// "edit:<cat>:<id>" — opens Monaco editor for one artifact.
struct EditPayload {
	std::string cat;
	std::string id;
	static std::optional<EditPayload> parse(const std::string& body) {
		auto parts = splitColon(body, 2);
		if (parts.size() != 2 || parts[0].empty() || parts[1].empty())
			return std::nullopt;
		return EditPayload{parts[0], parts[1]};
	}
};

// "save_artifact:<cat>:<id>:<base64-source>" — saves an edited artifact
// fork. The third part keeps trailing colons so the base64 blob is
// preserved verbatim.
struct SaveArtifactPayload {
	std::string cat;
	std::string id;
	std::string base64Source;
	static std::optional<SaveArtifactPayload> parse(const std::string& body) {
		auto parts = splitColon(body, 3);
		if (parts.size() != 3 || parts[0].empty() || parts[1].empty())
			return std::nullopt;
		return SaveArtifactPayload{parts[0], parts[1], parts[2]};
	}
};

// "world:<id>[:<seed>:<villagers>[:<urlencoded-name>]]" — new-world
// command from the picker. Optional fields default to template-friendly
// values (seed=42, villagers=0, name="My World") so older builders that
// only emit the id keep working.
struct WorldPayload {
	std::string id;
	int         seed     = 42;
	int         villagers= 0;
	std::string name     = "My World";
	static std::optional<WorldPayload> parse(const std::string& body) {
		auto parts = splitColon(body, 4);
		if (parts.empty() || parts[0].empty()) return std::nullopt;
		WorldPayload p;
		p.id = parts[0];
		if (parts.size() > 1) p.seed      = std::atoi(parts[1].c_str());
		if (parts.size() > 2) p.villagers = std::atoi(parts[2].c_str());
		if (parts.size() > 3) p.name      = parts[3];  // still URL-encoded
		if (p.seed <= 0) p.seed = 42;
		return p;
	}
};

// "join:<host>:<port>" — host may itself contain colons (IPv6), so we
// scan from the right.
struct JoinPayload {
	std::string host;
	int         port = 0;
	static std::optional<JoinPayload> parse(const std::string& body) {
		auto colon = body.rfind(':');
		if (colon == std::string::npos) return std::nullopt;
		JoinPayload p;
		p.host = body.substr(0, colon);
		p.port = std::atoi(body.substr(colon + 1).c_str());
		if (p.host.empty() || p.port <= 0) return std::nullopt;
		return p;
	}
};

// "mod:<ns>:<state>" — toggle a mod namespace. ns may itself contain
// colons (defensive — current namespaces don't), so scan from the right.
struct ModPayload {
	std::string ns;
	bool        wantOn = false;
	static std::optional<ModPayload> parse(const std::string& body) {
		auto colon = body.rfind(':');
		if (colon == std::string::npos) return std::nullopt;
		ModPayload p;
		p.ns     = body.substr(0, colon);
		p.wantOn = (body.substr(colon + 1) == "on");
		if (p.ns.empty()) return std::nullopt;
		return p;
	}
};

// "set:<key>:<value>" — settings setter. value may be a numeric, bool,
// or freeform string (theme id), so we pass it through as a string.
struct SetPayload {
	std::string key;
	std::string value;
	bool        valueBool() const { return (value == "true" || value == "1"); }
	float       valueFloat() const { return (float)std::atof(value.c_str()); }
	static std::optional<SetPayload> parse(const std::string& body) {
		auto colon = body.find(':');
		if (colon == std::string::npos) return std::nullopt;
		SetPayload p;
		p.key   = body.substr(0, colon);
		p.value = body.substr(colon + 1);
		if (p.key.empty()) return std::nullopt;
		return p;
	}
};

// URL-decode `%xx` and `+` → space. Used by WorldPayload::name and
// any other URL-encoded fields. Lives here so handlers don't reinvent
// the loop.
inline std::string urlDecode(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			int v = 0;
			if (std::sscanf(s.c_str() + i + 1, "%2x", &v) == 1) {
				out += (char)v;
				i += 2;
				continue;
			}
		}
		if (s[i] == '+') { out += ' '; continue; }
		out += s[i];
	}
	return out;
}

// Standard base64 decode. Used by SaveArtifactPayload to recover the
// edited source from the JS-side btoa() encoding.
inline std::string base64Decode(const std::string& b64) {
	std::string out;
	int v = 0, bits = 0;
	for (char c : b64) {
		if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
		int d = -1;
		if      (c >= 'A' && c <= 'Z') d = c - 'A';
		else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
		else if (c >= '0' && c <= '9') d = c - '0' + 52;
		else if (c == '+')             d = 62;
		else if (c == '/')             d = 63;
		else                           continue;
		v = (v << 6) | d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((v >> bits) & 0xff);
		}
	}
	return out;
}

} // namespace solarium::ui

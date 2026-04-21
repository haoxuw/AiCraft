#pragma once

// Persistent client identity. UUID lives in ~/.civcraft/client_id.json and
// survives across launches so the server can map this machine back to the same
// Seat. See docs/28_SEATS_AND_OWNERSHIP.md.
//
// TODO(steam): layer a Steam ID on top of this. The local UUID stays as a
// fallback for non-Steam installs; the server will prefer Steam ID when both
// are sent.
//
// TODO(keypair): the design doc calls for an ed25519 keypair alongside the
// UUID so the server can verify the claim. Deferred — Phase 1 trusts the UUID
// the client sends. Add a `"pubkey"` field to the JSON when we wire crypto.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace civcraft {

namespace detail {

inline std::filesystem::path identityDir() {
	const char* home = std::getenv("HOME");
	if (home && *home) return std::filesystem::path(home) / ".civcraft";
	// Fallback — /tmp is per-user ok for dev, visible-but-not-secret.
	return std::filesystem::path("/tmp") / ".civcraft";
}

inline std::filesystem::path identityFile() {
	return identityDir() / "client_id.json";
}

inline std::string generateUuid() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint32_t> dist;
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x%04x",
		dist(gen), dist(gen) & 0xFFFF, dist(gen) & 0xFFFF,
		dist(gen) & 0xFFFF, dist(gen), dist(gen) & 0xFFFF);
	return std::string(buf);
}

// Tiny ad-hoc parser for `"uuid": "..."`. Keeping it dependency-free on
// purpose — we control the file and only read what we wrote.
inline std::string extractUuidField(const std::string& text) {
	static const std::string key = "\"uuid\"";
	auto k = text.find(key);
	if (k == std::string::npos) return {};
	auto colon = text.find(':', k + key.size());
	if (colon == std::string::npos) return {};
	auto q1 = text.find('"', colon + 1);
	if (q1 == std::string::npos) return {};
	auto q2 = text.find('"', q1 + 1);
	if (q2 == std::string::npos) return {};
	return text.substr(q1 + 1, q2 - q1 - 1);
}

} // namespace detail

// Loads the persisted UUID or creates one on first run. Always returns a
// non-empty string; logs the path so the user can find/reset it.
inline std::string loadOrCreateClientUuid() {
	namespace fs = std::filesystem;
	fs::path path = detail::identityFile();

	// Read existing.
	if (fs::exists(path)) {
		std::ifstream f(path);
		if (f.is_open()) {
			std::stringstream ss;
			ss << f.rdbuf();
			std::string uuid = detail::extractUuidField(ss.str());
			if (!uuid.empty()) {
				std::printf("[Identity] %s → %s\n", path.c_str(), uuid.c_str());
				return uuid;
			}
			std::printf("[Identity] %s unparseable, regenerating\n", path.c_str());
		}
	}

	// Create.
	std::string uuid = detail::generateUuid();
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream f(path);
	if (f.is_open()) {
		f << "{\n";
		f << "  \"uuid\": \"" << uuid << "\"\n";
		f << "}\n";
		std::printf("[Identity] wrote %s → %s\n", path.c_str(), uuid.c_str());
	} else {
		// Non-fatal: we still return a UUID for this session. Next launch will
		// get a different one, but the game is playable.
		std::printf("[Identity] WARN: cannot write %s — using ephemeral UUID\n",
		            path.c_str());
	}
	return uuid;
}

} // namespace civcraft

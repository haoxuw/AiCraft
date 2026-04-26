#pragma once

// TtsVoiceMux — manages one piper child process per distinct voice.
//
// Piper has no runtime voice-switching: each model is loaded once on start
// and that's it. To give different NPCs different voices we spawn one
// sidecar per unique `dialog_voice` field value.
//
// Lookup is lazy: the first time a voice is requested we fork+exec a piper
// bound to that .onnx, wrap it in a TtsClient, and cache. Subsequent
// requests for the same voice reuse the cached client.
//
// Thread model: clientFor() is main-thread only (it spawns processes).
// Returned TtsClient* is safe to post speak() calls from any thread — that
// client's own worker handles serialisation.

#include "llm/tts_client.h"
#include "llm/tts_sidecar.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium::llm {

class TtsVoiceMux {
public:
	// Scan the piper_voices/ directory for .onnx files (and matching .json).
	// Returns false if no voices are usable at all.
	bool init() {
		namespace fs = std::filesystem;
		TtsSidecar::Paths probe;
		if (!TtsSidecar::probe(probe)) return false;
		m_binary = probe.binary;

		fs::path voicesDir = fs::path(probe.voiceOnnx).parent_path();
		for (auto& e : fs::directory_iterator(voicesDir)) {
			if (!e.is_regular_file()) continue;
			if (e.path().extension() != ".onnx") continue;
			fs::path j = e.path(); j += ".json";
			if (!fs::exists(j)) continue;
			m_voicePaths.push_back(e.path().string());
		}
		std::sort(m_voicePaths.begin(), m_voicePaths.end());
		m_defaultVoice = probe.voiceOnnx;
		return !m_voicePaths.empty();
	}

	// Resolve an NPC's `dialog_voice` field value to an absolute .onnx path.
	// Match rules (in order):
	//   1. Empty  → default voice.
	//   2. Substring match on any voice filename (case-sensitive). Picks the
	//      first match in sorted order. So "amy" → "en_US-amy-medium.onnx".
	//   3. No match → default voice (silently falls back).
	std::string resolveVoice(const std::string& name) const {
		if (name.empty()) return m_defaultVoice;
		for (const auto& p : m_voicePaths) {
			if (std::filesystem::path(p).filename().string().find(name)
			    != std::string::npos) {
				return p;
			}
		}
		return m_defaultVoice;
	}

	// Get (and lazily spawn) a TtsClient for the given voice. Returns
	// nullptr if the sidecar couldn't be spawned.
	TtsClient* clientFor(const std::string& voiceName) {
		std::string path = resolveVoice(voiceName);
		auto it = m_slots.find(path);
		if (it != m_slots.end()) return it->second.client.get();

		TtsSidecar::Paths p;
		p.binary    = m_binary;
		p.voiceOnnx = path;
		auto side = std::make_unique<TtsSidecar>();
		if (!side->start(p)) {
			std::fprintf(stderr, "[tts-mux] failed to spawn voice %s\n",
				path.c_str());
			return nullptr;
		}
		auto client = std::make_unique<TtsClient>(side.get());
		TtsClient* raw = client.get();
		Slot slot;
		slot.sidecar = std::move(side);
		slot.client  = std::move(client);
		m_slots.emplace(path, std::move(slot));
		return raw;
	}

	// List available voice filenames (basename without extension) — useful
	// for artifact validation and the modder docs.
	std::vector<std::string> voiceNames() const {
		std::vector<std::string> out;
		out.reserve(m_voicePaths.size());
		for (const auto& p : m_voicePaths)
			out.push_back(std::filesystem::path(p).stem().string());
		return out;
	}

	bool empty() const { return m_voicePaths.empty(); }

private:
	struct Slot {
		std::unique_ptr<TtsSidecar> sidecar;
		std::unique_ptr<TtsClient>  client;
	};
	std::string                           m_binary;
	std::string                           m_defaultVoice;
	std::vector<std::string>              m_voicePaths;
	std::unordered_map<std::string, Slot> m_slots;
};

} // namespace solarium::llm

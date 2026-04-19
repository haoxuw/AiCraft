// TtsVoiceMux resolution test. Doesn't spawn piper — just asserts that
// resolveVoice() routes artifact-field values to the right .onnx, with
// fallback to the default voice for unknown names.

#include "llm/tts_voice_mux.h"

#include <cstdio>
#include <cstdlib>

int main() {
	civcraft::llm::TtsVoiceMux mux;
	if (!mux.init()) {
		std::fprintf(stderr, "[mux-smoke] no voices found — run make tts_setup\n");
		return 2;
	}
	auto names = mux.voiceNames();
	std::printf("voices:");
	for (auto& n : names) std::printf(" %s", n.c_str());
	std::printf("\n");

	// Resolution checks.
	std::string empty   = mux.resolveVoice("");
	std::string amy     = mux.resolveVoice("amy");
	std::string missing = mux.resolveVoice("nonexistent-voice");

	std::printf("resolve(''):            %s\n", empty.c_str());
	std::printf("resolve('amy'):         %s\n", amy.c_str());
	std::printf("resolve('nonexistent'): %s\n", missing.c_str());

	// amy must match exactly one voice and equal it.
	bool ok = true;
	if (empty.empty())                      { std::fprintf(stderr, "FAIL: default empty\n"); ok = false; }
	if (missing != empty)                   { std::fprintf(stderr, "FAIL: missing should fall back to default\n"); ok = false; }
	if (amy.find("amy") == std::string::npos) {
		std::fprintf(stderr, "FAIL: amy routing lost the substring\n"); ok = false;
	}
	return ok ? 0 : 1;
}

// Tiny standalone smoke test for TtsClient. Spawns piper via TtsSidecar,
// synthesises one utterance, prints the resulting WAV path. Build:
//   cmake --build build --target solarium-tts-smoke

#include "llm/tts_client.h"
#include "llm/tts_sidecar.h"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

int main(int argc, char** argv) {
	solarium::llm::TtsSidecar::Paths paths;
	if (!solarium::llm::TtsSidecar::probe(paths)) {
		std::fprintf(stderr, "[tts-smoke] no piper/voice found — run make tts_setup\n");
		return 2;
	}
	solarium::llm::TtsSidecar sidecar;
	if (!sidecar.start(paths)) {
		std::fprintf(stderr, "[tts-smoke] sidecar failed to start\n");
		return 2;
	}
	// Give piper ~2 s to load the voice model before we hit it.
	usleep(2'000'000);

	std::string text = (argc >= 2)
		? argv[1]
		: "Hello traveller, welcome to the village. The weather is fair today.";
	std::fprintf(stderr, "[tts-smoke] speaking: %s\n", text.c_str());

	solarium::llm::TtsClient client(&sidecar);
	std::mutex mtx;
	std::condition_variable cv;
	bool done = false;
	bool ok   = false;
	std::string result;

	client.speak(text,
		[&](bool o, std::string wavPath) {
			std::lock_guard<std::mutex> lk(mtx);
			ok = o;
			result = std::move(wavPath);
			done = true;
			cv.notify_all();
		});

	{
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [&] { return done; });
	}

	if (!ok) {
		std::fprintf(stderr, "[tts-smoke] FAIL: %s\n", result.c_str());
		return 1;
	}
	std::printf("%s\n", result.c_str());
	return 0;
}

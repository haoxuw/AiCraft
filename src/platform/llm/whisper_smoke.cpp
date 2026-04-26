// Tiny standalone smoke test for WhisperClient. Reads a WAV from argv[1],
// submits it to whisper-server on :8081, prints the transcript. Built only
// on demand via `cmake --build build --target solarium-whisper-smoke`.

#include "llm/whisper_client.h"

#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <audio.wav>\n", argv[0]);
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
	std::fprintf(stderr, "[smoke] %zu bytes → whisper-server\n", bytes.size());

	solarium::llm::WhisperClient::Config cfg;
	cfg.host = "127.0.0.1";
	cfg.port = 8081;
	solarium::llm::WhisperClient client(cfg);

	std::mutex mtx;
	std::condition_variable cv;
	bool done = false;
	bool ok   = false;
	std::string result;

	client.transcribe(std::move(bytes),
		[&](bool o, std::string text) {
			std::lock_guard<std::mutex> lk(mtx);
			ok = o;
			result = std::move(text);
			done = true;
			cv.notify_all();
		});

	{
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [&] { return done; });
	}

	if (!ok) {
		std::fprintf(stderr, "[smoke] FAIL: %s\n", result.c_str());
		return 1;
	}
	std::printf("%s\n", result.c_str());
	return 0;
}

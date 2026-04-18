#pragma once

// LlmSidecar — launches `llama-server` as a child of the player client so a
// single `make game` boots everything: world server, GUI client, and the
// local LLM that powers NPC dialog. No separate terminal needed.
//
// Lifecycle:
//   probe()      — returns true if both the llama-server binary AND at least
//                  one .gguf model are on disk. Called before start() so we
//                  can show a clear "run `make llm_setup`" notification if
//                  the user skipped the one-time download.
//   start()      — fork + execv; stdout/stderr redirected to
//                  /tmp/civcraft_llm.log so llama-server chatter doesn't
//                  drown the game log. Non-blocking — model load takes
//                  several seconds, but LlmClient::health() polls for ready.
//   stop()       — SIGTERM → wait → SIGKILL fallback. Called on shutdown.
//
// Search paths (relative to CWD, which is always build*/): walks "../llm/"
// — the repo-root folder populated by `make llm_setup`. For Steam packaging
// we'd add a POST_BUILD copy into the install tree; path probe stays the same.

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace civcraft::llm {

class LlmSidecar {
public:
	struct Paths {
		std::string binary;
		std::string model;
	};

	~LlmSidecar() { stop(); }

	LlmSidecar(const LlmSidecar&) = delete;
	LlmSidecar& operator=(const LlmSidecar&) = delete;
	LlmSidecar() = default;

	// Scan the repo-relative llm/ tree for the binary + a .gguf model.
	// Returns a populated Paths iff both are found.
	static bool probe(Paths& out) {
		namespace fs = std::filesystem;
		// From the binary's CWD (build/ or build-perf/), llm/ lives one up.
		const std::string roots[] = { "../llm", "llm" };
		for (const auto& root : roots) {
			std::string bin = root + "/llama.cpp/build/bin/llama-server";
			if (!fs::exists(bin)) continue;

			fs::path modelsDir = fs::path(root) / "models";
			if (!fs::is_directory(modelsDir)) continue;

			// Pick the first .gguf we see. If the user has multiple models,
			// deterministic order via sorted filename.
			std::vector<fs::path> ggufs;
			for (auto& e : fs::directory_iterator(modelsDir)) {
				if (e.is_regular_file() && e.path().extension() == ".gguf")
					ggufs.push_back(e.path());
			}
			if (ggufs.empty()) continue;
			std::sort(ggufs.begin(), ggufs.end());

			out.binary = fs::absolute(bin).string();
			out.model  = fs::absolute(ggufs.front()).string();
			return true;
		}
		return false;
	}

	// Spawn llama-server on 127.0.0.1:port. Non-blocking; caller (LlmClient)
	// polls health() to know when the model's actually loaded.
	bool start(const Paths& paths, int port = 8080, int ctxSize = 2048) {
		if (m_pid > 0) return true; // already running

		std::vector<std::string> args = {
			paths.binary,
			"--host", "127.0.0.1",
			"--port", std::to_string(port),
			"--ctx-size", std::to_string(ctxSize),
			"--n-predict", "200",
			"-m", paths.model,
		};

		pid_t pid = fork();
		if (pid < 0) {
			std::fprintf(stderr, "[llm-sidecar] fork failed: %s\n", std::strerror(errno));
			return false;
		}
		if (pid == 0) {
			// Child — redirect stdio to the log file so the game console
			// stays readable. llama-server is chatty on startup.
			int fd = open("/tmp/civcraft_llm.log",
			              O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) {
				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
			// Put child in its own process group so Ctrl-C on the game
			// terminal doesn't SIGINT llama-server before we can SIGTERM it
			// cleanly in stop().
			setpgid(0, 0);

			std::vector<char*> argv;
			for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
			argv.push_back(nullptr);
			execv(argv[0], argv.data());
			_exit(127);
		}

		m_pid  = pid;
		m_port = port;
		std::printf("[llm-sidecar] spawned llama-server pid=%d port=%d model=%s\n",
		            (int)pid, port,
		            std::filesystem::path(paths.model).filename().string().c_str());
		std::printf("[llm-sidecar] sidecar log: /tmp/civcraft_llm.log\n");
		return true;
	}

	void stop() {
		if (m_pid <= 0) return;
		if (isAlive()) {
			::kill(m_pid, SIGTERM);
			for (int i = 0; i < 30; ++i) {
				usleep(100'000);
				if (!isAlive()) break;
			}
			if (isAlive()) ::kill(m_pid, SIGKILL);
		}
		int status = 0;
		waitpid(m_pid, &status, 0);
		m_pid = 0;
		std::printf("[llm-sidecar] stopped.\n");
	}

	bool running() const { return m_pid > 0 && isAlive(); }
	int  port()    const { return m_port; }

private:
	bool isAlive() const { return m_pid > 0 && ::kill(m_pid, 0) == 0; }

	pid_t m_pid  = 0;
	int   m_port = 0;
};

} // namespace civcraft::llm

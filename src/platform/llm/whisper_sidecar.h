#pragma once

// WhisperSidecar — auto-spawns whisper.cpp's `whisper-server` as a child of
// solarium-ui-vk so `make game` brings up STT for the dialog panel.
//
// Mirror of LlmSidecar (llm_sidecar.h). Same lifecycle:
//   probe()  — find binary + ggml model on disk
//   start()  — fork+execv, stdio → /tmp/solarium_whisper.log, isolated pgrp
//   stop()   — SIGTERM → wait → SIGKILL fallback
//
// Runs on port 8081 (LLM is 8080, piper is 8082). HTTP API:
//   POST /inference  — multipart/form-data with a "file" field (WAV/FLAC/…)
//                      returns JSON { "text": "...", ...}

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

namespace solarium::llm {

class WhisperSidecar {
public:
	struct Paths {
		std::string binary;
		std::string model;
	};

	~WhisperSidecar() { stop(); }

	WhisperSidecar(const WhisperSidecar&) = delete;
	WhisperSidecar& operator=(const WhisperSidecar&) = delete;
	WhisperSidecar() = default;

	// Scan the repo-relative llm/ tree for whisper-server binary + a ggml
	// model. Returns populated Paths iff both exist.
	static bool probe(Paths& out) {
		namespace fs = std::filesystem;
		const std::string roots[] = { "../llm", "llm" };
		for (const auto& root : roots) {
			std::string bin = root + "/whisper.cpp/build/bin/whisper-server";
			if (!fs::exists(bin)) continue;

			fs::path modelsDir = fs::path(root) / "whisper_models";
			if (!fs::is_directory(modelsDir)) continue;

			std::vector<fs::path> models;
			for (auto& e : fs::directory_iterator(modelsDir)) {
				if (e.is_regular_file() && e.path().extension() == ".bin")
					models.push_back(e.path());
			}
			if (models.empty()) continue;
			std::sort(models.begin(), models.end());

			out.binary = fs::absolute(bin).string();
			out.model  = fs::absolute(models.front()).string();
			return true;
		}
		return false;
	}

	bool start(const Paths& paths, int port = 8081) {
		if (m_pid > 0) return true;

		std::vector<std::string> args = {
			paths.binary,
			"--host", "127.0.0.1",
			"--port", std::to_string(port),
			"-m", paths.model,
		};

		pid_t pid = fork();
		if (pid < 0) {
			std::fprintf(stderr, "[whisper-sidecar] fork failed: %s\n", std::strerror(errno));
			return false;
		}
		if (pid == 0) {
			int fd = open("/tmp/solarium_whisper.log",
			              O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) {
				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
			setpgid(0, 0);

			std::vector<char*> argv;
			for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
			argv.push_back(nullptr);
			execv(argv[0], argv.data());
			_exit(127);
		}

		m_pid  = pid;
		m_port = port;
		std::printf("[whisper-sidecar] spawned whisper-server pid=%d port=%d model=%s\n",
		            (int)pid, port,
		            std::filesystem::path(paths.model).filename().string().c_str());
		std::printf("[whisper-sidecar] log: /tmp/solarium_whisper.log\n");
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
		std::printf("[whisper-sidecar] stopped.\n");
	}

	bool running() const { return m_pid > 0 && isAlive(); }
	int  port()    const { return m_port; }

private:
	bool isAlive() const { return m_pid > 0 && ::kill(m_pid, 0) == 0; }

	pid_t m_pid  = 0;
	int   m_port = 0;
};

} // namespace solarium::llm

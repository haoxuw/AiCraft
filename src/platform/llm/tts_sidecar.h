#pragma once

// TtsSidecar — auto-spawns piper (https://github.com/rhasspy/piper) as a
// child of civcraft-ui-vk so `make game` brings up TTS for NPC dialog.
//
// Unlike llama-server / whisper-server, piper has no HTTP mode. Instead:
//   * We launch it once with --json-input and pipe its stdin/stdout.
//   * Each synthesis request is one JSON line:
//       {"text": "...", "output_file": "/tmp/civcraft_piper_<N>.wav"}
//     Piper writes the WAV and emits the path + duration on stdout.
//   * The client-side TtsClient (follow-up file) watches the output file
//     (or the stdout line) and hands the WAV to miniaudio.
//
// Long-lived process amortises the 1-2s voice model load. Each utterance
// round-trip is typically 150-500ms for a short sentence on CPU.
//
// Lifecycle mirrors LlmSidecar: probe() → start() → stop().

#include <algorithm>
#include <cerrno>
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

class TtsSidecar {
public:
	struct Paths {
		std::string binary;
		std::string voiceOnnx;  // en_US-amy-medium.onnx
	};

	~TtsSidecar() { stop(); }

	TtsSidecar(const TtsSidecar&) = delete;
	TtsSidecar& operator=(const TtsSidecar&) = delete;
	TtsSidecar() = default;

	static bool probe(Paths& out) {
		namespace fs = std::filesystem;
		const std::string roots[] = { "../llm", "llm" };
		for (const auto& root : roots) {
			std::string bin = root + "/piper/piper";
			if (!fs::exists(bin)) continue;

			fs::path voicesDir = fs::path(root) / "piper_voices";
			if (!fs::is_directory(voicesDir)) continue;

			std::vector<fs::path> voices;
			for (auto& e : fs::directory_iterator(voicesDir)) {
				if (!e.is_regular_file()) continue;
				if (e.path().extension() != ".onnx") continue;
				// Require the companion .json or the voice won't load.
				fs::path json = e.path();
				json += ".json";
				if (!fs::exists(json)) continue;
				voices.push_back(e.path());
			}
			if (voices.empty()) continue;
			std::sort(voices.begin(), voices.end());

			out.binary    = fs::absolute(bin).string();
			out.voiceOnnx = fs::absolute(voices.front()).string();
			return true;
		}
		return false;
	}

	// Spawn piper with --json-input so each stdin line is a synthesis request.
	// Stdin/stdout handles are kept on the parent side as m_stdinFd/m_stdoutFd
	// so the client can submit requests and read responses.
	bool start(const Paths& paths) {
		if (m_pid > 0) return true;

		int inPipe[2]  = {-1, -1};  // parent writes → child stdin
		int outPipe[2] = {-1, -1};  // child stdout → parent reads
		if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
			std::fprintf(stderr, "[tts-sidecar] pipe(): %s\n", std::strerror(errno));
			return false;
		}

		std::vector<std::string> args = {
			paths.binary,
			"--model", paths.voiceOnnx,
			"--json-input",
			"--output_dir", "/tmp",
		};

		pid_t pid = fork();
		if (pid < 0) {
			std::fprintf(stderr, "[tts-sidecar] fork: %s\n", std::strerror(errno));
			::close(inPipe[0]); ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);
			return false;
		}
		if (pid == 0) {
			// Child: stdin ← inPipe[0], stdout/stderr → outPipe[1].
			// Piper's status lines go to stderr; routing both to the same
			// pipe keeps the protocol on one stream.
			dup2(inPipe[0],  STDIN_FILENO);
			dup2(outPipe[1], STDOUT_FILENO);
			// Errors and "diagnostic" info still go to a file so the parent
			// pipe isn't polluted with load-time noise.
			int errFd = open("/tmp/civcraft_piper.log",
			                 O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (errFd >= 0) { dup2(errFd, STDERR_FILENO); ::close(errFd); }
			::close(inPipe[0]);  ::close(inPipe[1]);
			::close(outPipe[0]); ::close(outPipe[1]);
			setpgid(0, 0);

			// piper needs LD_LIBRARY_PATH to find the bundled onnxruntime libs.
			std::string libDir = std::filesystem::path(paths.binary).parent_path().string();
			setenv("LD_LIBRARY_PATH", libDir.c_str(), 1);

			std::vector<char*> argv;
			for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
			argv.push_back(nullptr);
			execv(argv[0], argv.data());
			_exit(127);
		}

		::close(inPipe[0]);
		::close(outPipe[1]);
		m_stdinFd  = inPipe[1];
		m_stdoutFd = outPipe[0];
		m_pid      = pid;

		std::printf("[tts-sidecar] spawned piper pid=%d voice=%s\n", (int)pid,
		            std::filesystem::path(paths.voiceOnnx).filename().string().c_str());
		std::printf("[tts-sidecar] log: /tmp/civcraft_piper.log\n");
		return true;
	}

	void stop() {
		if (m_stdinFd >= 0) { ::close(m_stdinFd); m_stdinFd = -1; }
		if (m_stdoutFd >= 0) { ::close(m_stdoutFd); m_stdoutFd = -1; }
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
		std::printf("[tts-sidecar] stopped.\n");
	}

	bool running() const { return m_pid > 0 && isAlive(); }

	// Exposed so the (forthcoming) TtsClient can write JSON requests and
	// read response lines. Raw fds keep this header dependency-free.
	int stdinFd()  const { return m_stdinFd; }
	int stdoutFd() const { return m_stdoutFd; }

private:
	bool isAlive() const { return m_pid > 0 && ::kill(m_pid, 0) == 0; }

	pid_t m_pid      = 0;
	int   m_stdinFd  = -1;
	int   m_stdoutFd = -1;
};

} // namespace civcraft::llm

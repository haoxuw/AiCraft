#pragma once

/**
 * AgentManager — spawns and manages the server process for singleplayer.
 *
 * Used by the GUI (player client) to orchestrate singleplayer:
 *   1. Spawn modcraft-server on a free port
 *   2. Wait for server ready signal
 *   3. GUI connects to localhost as a regular player client
 *
 * AI agent processes are spawned by the server itself (via ClientManager).
 * On shutdown, signals server to save and stop (which also kills its agents).
 */

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <filesystem>

namespace modcraft {

class AgentManager {
public:
	struct Config {
		int seed = 42;
		int templateIndex = 1;
		int port = 0;            // 0 = auto-pick free port (7800-7899)
		std::string worldPath;   // load saved world (empty = new world)
		std::string execDir;     // directory containing modcraft-* binaries
	};

	~AgentManager() { stopAll(); }

	// Launch server process. Returns port on success, -1 on failure.
	int launchServer(const Config& cfg) {
		m_port = (cfg.port > 0) ? cfg.port : findFreePort();
		if (m_port < 0) return -1;

		std::string serverBin = cfg.execDir + "/modcraft-server";
		if (!std::filesystem::exists(serverBin)) {
			printf("[AgentManager] Server binary not found: %s\n", serverBin.c_str());
			return -1;
		}

		std::vector<std::string> args = {serverBin, "--port", std::to_string(m_port)};
		if (!cfg.worldPath.empty()) {
			args.push_back("--world");
			args.push_back(cfg.worldPath);
		} else {
			args.push_back("--seed");
			args.push_back(std::to_string(cfg.seed));
			args.push_back("--template");
			args.push_back(std::to_string(cfg.templateIndex));
		}

		char logPath[64];
		snprintf(logPath, sizeof(logPath), "/tmp/modcraft_log_%d.log", m_port);
		m_serverPid = spawnProcess(args, logPath);
		if (m_serverPid <= 0) {
			printf("[AgentManager] Failed to spawn server\n");
			return -1;
		}

		char readyPath[64];
		snprintf(readyPath, sizeof(readyPath), "/tmp/modcraft_ready_%d", m_port);
		if (!waitForFile(readyPath, 5.0f)) {
			printf("[AgentManager] Server failed to start (timeout)\n");
			kill(m_serverPid, SIGKILL);
			int status;
			waitpid(m_serverPid, &status, 0);
			m_serverPid = 0;
			return -1;
		}

		printf("[AgentManager] Server started on port %d (pid %d)\n", m_port, m_serverPid);
		return m_port;
	}

	// Stop server process (server will stop its own AI agent children).
	void stopAll() {
		if (m_serverPid > 0) {
			if (isAlive(m_serverPid)) {
				kill(m_serverPid, SIGTERM);
				for (int i = 0; i < 30; i++) {
					usleep(100000);
					if (!isAlive(m_serverPid)) break;
				}
				if (isAlive(m_serverPid))
					kill(m_serverPid, SIGKILL);
			}
			int status;
			waitpid(m_serverPid, &status, 0);
			m_serverPid = 0;
		}

		if (m_port > 0) {
			char readyPath[64];
			snprintf(readyPath, sizeof(readyPath), "/tmp/modcraft_ready_%d", m_port);
			std::remove(readyPath);
		}

		printf("[AgentManager] All processes stopped.\n");
	}

	bool isServerAlive() const { return m_serverPid > 0 && isAlive(m_serverPid); }
	int port() const { return m_port; }

private:
	static pid_t spawnProcess(const std::vector<std::string>& args, const char* logPath = nullptr) {
		pid_t pid = fork();
		if (pid == 0) {
			std::vector<char*> cargs;
			for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
			cargs.push_back(nullptr);
			int outFd = logPath ? open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
			if (outFd < 0) outFd = open("/dev/null", O_WRONLY);
			if (outFd >= 0) {
				dup2(outFd, STDOUT_FILENO);
				dup2(outFd, STDERR_FILENO);
				close(outFd);
			}
			execv(cargs[0], cargs.data());
			_exit(127);
		}
		return pid;
	}

	static bool isAlive(pid_t pid) { return kill(pid, 0) == 0; }

	static bool waitForFile(const char* path, float timeoutSec) {
		int maxWait = (int)(timeoutSec * 20);
		for (int i = 0; i < maxWait; i++) {
			if (std::filesystem::exists(path)) return true;
			usleep(50000);
		}
		return false;
	}

	// Find a free port by attempting a real bind — immune to stale ready-files.
	// Also cleans up stale ready-files for ports that are no longer in use.
	static int findFreePort() {
		for (int p = 7800; p < 7900; p++) {
			// Try to bind — if it succeeds the port is free
			int fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd < 0) continue;
			int opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
			sockaddr_in addr{};
			addr.sin_family      = AF_INET;
			addr.sin_addr.s_addr = INADDR_ANY;
			addr.sin_port        = htons(p);
			bool free = (bind(fd, (sockaddr*)&addr, sizeof(addr)) == 0);
			close(fd);
			if (free) {
				// Remove any stale ready-file left from a previous crash
				char stale[64];
				snprintf(stale, sizeof(stale), "/tmp/modcraft_ready_%d", p);
				std::remove(stale);
				return p;
			}
		}
		return -1;
	}

	int m_port = -1;
	pid_t m_serverPid = 0;
};

} // namespace modcraft

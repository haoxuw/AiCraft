#pragma once

// Spawns civcraft-server for singleplayer; GUI then connects over localhost.
// AI agent processes are spawned by the server itself via ClientManager.

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <netinet/in.h>
#include <filesystem>

namespace civcraft {

class AgentManager {
public:
	struct Config {
		int seed = 42;
		int templateIndex = 1;
		int port = 0;            // 0 = auto-pick in [7800, 7900)
		int villagersOverride = 0;  // >0 → CIVCRAFT_VILLAGERS=N inherited by server
		float simSpeed = 1.0f;   // >0; passed to spawned server as --sim-speed
		std::string worldPath;   // empty = new world
		std::string execDir;
	};

	~AgentManager() { stopAll(); }

	// Returns port on success, -1 on failure.
	int launchServer(const Config& cfg) {
		m_port = (cfg.port > 0) ? cfg.port : findFreePort();
		if (m_port < 0) return -1;

		std::string serverBin = cfg.execDir + "/civcraft-server";
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

		// Client-chosen villager override: env var inherits across fork() into
		// the spawned server, which reads it in spawnMobsForClient (mirrors
		// the existing CIVCRAFT_STRESS_MULT pattern). Keeps the server host
		// agnostic — the client decides the population.
		if (cfg.villagersOverride > 0) {
			setenv("CIVCRAFT_VILLAGERS",
			       std::to_string(cfg.villagersOverride).c_str(), 1);
		}

		if (cfg.simSpeed > 0.0f && cfg.simSpeed != 1.0f) {
			args.push_back("--sim-speed");
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%g", cfg.simSpeed);
			args.push_back(buf);
		}

		// nullptr = inherit GUI stdout/stderr so server + its spawned agents log here.
		m_serverPid = spawnProcess(args, nullptr);
		if (m_serverPid <= 0) {
			printf("[AgentManager] Failed to spawn server\n");
			return -1;
		}

		char readyPath[64];
		snprintf(readyPath, sizeof(readyPath), "/tmp/civcraft_ready_%d", m_port);
		// 30s covers cold-start world gen on slower machines / first-run
		// without Python bytecode cache. The live path hits this in well
		// under 1s once warmed up.
		if (!waitForFile(readyPath, 30.0f)) {
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

	// Server stops its own agent children on SIGTERM.
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
			snprintf(readyPath, sizeof(readyPath), "/tmp/civcraft_ready_%d", m_port);
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
			// Kernel-enforced: if the parent (client) dies for any reason
			// (crash, SIGKILL, terminal close) send SIGTERM to this child so
			// the spawned server can't outlive it and occupy the port. The
			// explicit stopAll() path in ~AgentManager still runs on clean
			// shutdown — this is the belt to that suspenders.
			prctl(PR_SET_PDEATHSIG, SIGTERM);
			// Race: parent may have already died between fork() and the
			// prctl above. Check and bail if so.
			if (getppid() == 1) _exit(0);

			std::vector<char*> cargs;
			for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
			cargs.push_back(nullptr);
			if (logPath) {
				int outFd = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (outFd >= 0) {
					dup2(outFd, STDOUT_FILENO);
					dup2(outFd, STDERR_FILENO);
					close(outFd);
				}
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

	// Bind-probe port search — immune to stale ready-files (cleans them too).
	static int findFreePort() {
		for (int p = 7800; p < 7900; p++) {
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
				char stale[64];
				snprintf(stale, sizeof(stale), "/tmp/civcraft_ready_%d", p);
				std::remove(stale);
				return p;
			}
		}
		return -1;
	}

	int m_port = -1;
	pid_t m_serverPid = 0;
};

} // namespace civcraft
